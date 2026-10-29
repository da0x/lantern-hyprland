// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/activate-page.hpp"

#include "activation/activator.hpp"
#include "application/settings.hpp"
#include "application/lantern-folder.hpp"
#include "configuration/main-config.hpp"
#include "configuration/recording.hpp"
#include "desktop/process.hpp"
#include "desktop/system.hpp"
#include "foundation/environment.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"
#include "ui/widgets.hpp"

#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/Text.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <fstream>
#include <sstream>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprtoolkit::CFontSize;
        using Hyprutils::Memory::CSharedPointer;

        // What each area is, for its card.
        struct AreaText {
            std::string_view title;
            std::string_view about;
            // A symbol from the Nerd Font the bar's icons come from.
            std::string_view symbol;
        };

        AreaText textOf(activation::Area area) {
            switch (area) {
                case activation::Area::keybindings:
                    return {"Keybindings", "hl.bind calls. Taken back by reloading Hyprland.", "\U000F030C"};
                case activation::Area::programs:
                    return {"Programs", "Programs you installed, from hl.exec_cmd lines. Started by Lantern, and only those processes stopped again.", "\U000F08C6"};
                case activation::Area::theme:
                    return {"Desktop theme", "Nothing to activate yet: the Theme page writes your theme to its files instead, after you review the change.", "\U000F03D8"};
                case activation::Area::options: break;
            }
            return {"Options and rules", "hl.config, window, layer and workspace rules, animations and monitors. Taken back by reloading Hyprland.", "\U000F062E"};
        }

        std::optional<std::filesystem::path> hyprctl() {
            return desktop::findProgram("hyprctl", foundation::variable(foundation::currentEnvironment(), "PATH").value_or(""));
        }

        std::string now() {
            const auto time = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::system_clock::now()};
            return std::format("{:%H:%M}", time);
        }

        CSharedPointer<Hyprtoolkit::CRectangleElement> panel(const CSharedPointer<Hyprtoolkit::CPalette>& palette, CDynamicSize size, bool highlighted) {
            return Hyprtoolkit::CRectangleBuilder::begin()
                ->color([palette] { return palette->m_colors.alternateBase; })
                ->rounding(palette->m_vars.bigRounding)
                ->borderColor([palette, highlighted] { return highlighted ? palette->m_colors.accent : Hyprtoolkit::CHyprColor{0, 0, 0, 0}; })
                ->borderThickness(2)
                ->size(std::move(size))
                ->commence();
        }

        // The names of the areas in a record, as a sentence's list.
        std::string listed(const activation::Record& record) {
            std::vector<std::string> parts;
            for (const auto area : record.areas) {
                if (area == activation::Area::programs)
                    parts.push_back(record.programs.size() == 1 ? "1 program" : std::format("{} programs", record.programs.size()));
                else
                    parts.emplace_back(textOf(area).title);
            }
            // "Options and rules, keybindings and 1 program": capitals only at the start.
            std::string text;
            for (std::size_t index = 0; index < parts.size(); ++index) {
                auto part = parts[index];
                if (index > 0 && !part.empty())
                    part[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(part[0])));
                text += std::format("{}{}", index == 0 ? "" : index + 1 == parts.size() ? " and " : ", ", part);
            }
            return text;
        }

    }

    ActivatePage::ActivatePage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer) :
        palette{std::move(palette)}, defer{std::move(defer)} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        root->addChild(pageTitle("Activate", this->palette));
        root->addChild(paragraph("Try effects on the running desktop, and take them all back in one click. Nothing is written to your config. The effects are "
                                 "plain Hyprland Lua in Lantern's effects file, which you can edit, and copy into your own config to keep.",
                                 this->palette));
        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        root->addChild(body);
        footer = leftRow();
        root->addChild(footer);

        const auto folder = application::lanternFolder(foundation::currentEnvironment());
        for (const auto& name : application::loadSettings(folder).activationAllowed)
            if (const auto area = activation::areaNamed(name))
                allowedAreas.push_back(*area);
        // Left on when Lantern last closed, or stopped without warning.
        active = activation::loadRecord(folder);
        readEffects();
        showOverview();
    }

    void ActivatePage::restyle() {
        showOverview();
    }

    bool ActivatePage::allowed(activation::Area area) const {
        return std::ranges::contains(allowedAreas, area);
    }

    void ActivatePage::readEffects() {
        const auto environment = foundation::currentEnvironment();
        const auto file        = activation::effectsFile(application::lanternFolder(environment));
        std::error_code error;
        if (!std::filesystem::exists(file, error)) {
            std::filesystem::create_directories(file.parent_path(), error);
            if (const auto written = configuration::writeWhole(file, activation::effectsTemplate()); !written)
                outcome = std::format("The effects file could not be made: {}.", written.error());
        }
        std::ifstream     stream{file, std::ios::binary};
        std::stringstream text;
        text << stream.rdbuf();
        effects = activation::readEffects(text.str());

        // The user's own bindings, read the way the Keyboard shortcuts page reads them.
        collisions.clear();
        if (!effects.in(activation::Area::keybindings).empty())
            if (const auto config = configuration::mainConfig(configuration::lanternLaunch()); config && config->lua())
                if (const auto recording = configuration::record(config->file))
                    collisions = activation::collisions(effects, configuration::bindings(*recording));
    }

    void ActivatePage::allow(activation::Area area, bool allowedNow) {
        std::erase(allowedAreas, area);
        if (allowedNow)
            allowedAreas.push_back(area);
        const auto folder   = application::lanternFolder(foundation::currentEnvironment());
        auto       settings = application::loadSettings(folder);
        settings.activationAllowed.clear();
        for (const auto each : activation::areas)
            if (allowed(each))
                settings.activationAllowed.emplace_back(activation::nameOf(each));
        if (const auto saved = application::saveSettings(settings, folder); !saved)
            outcome = std::format("Lantern could not save its settings: {}.", saved.error());
        showOverview();
    }

    void ActivatePage::activate() {
        if (active)
            return;
        readEffects();
        std::vector<activation::Area> applying;
        for (const auto area : activation::areas)
            if (allowed(area))
                applying.push_back(area);
        auto done = activation::activate(effects, applying, hyprctl(), now());
        problems  = std::move(done.problems);
        if (done.record.areas.empty()) {
            outcome = applying.empty() ? "Allow an area first: nothing is activated until you do." : "Nothing to activate: the allowed areas have no effects in the file.";
            return showOverview();
        }
        const auto folder = application::lanternFolder(foundation::currentEnvironment());
        if (const auto saved = activation::saveRecord(done.record, folder); !saved)
            problems.push_back(std::format("The record of what is active could not be saved, so Deactivate after closing Lantern will not know of it: {}.", saved.error()));
        active  = std::move(done.record);
        outcome = problems.empty() ? "Activated." : "Activated, with problems.";
        showOverview();
    }

    void ActivatePage::deactivate() {
        if (!active)
            return;
        problems = activation::deactivate(*active, hyprctl());
        activation::removeRecord(application::lanternFolder(foundation::currentEnvironment()));
        active.reset();
        outcome = problems.empty() ? "Deactivated: the desktop is as your config says." : "Deactivated, with problems.";
        showOverview();
    }

    void ActivatePage::showOverview() {
        body->clearChildren();
        const auto accent = hex(palette->m_colors.accent);

        // The state, and the one button that changes it.
        const auto state = panel(palette, CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, scaled(84)}}, active.has_value());
        const auto line  = leftRow(84);
        line->setMargin(16);
        const auto words = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(4)->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        words->setGrow(true, false);
        if (active) {
            words->addChild(markupLine(std::format("<span weight=\"600\">{}</span>", colored(std::format("Active since {}", active->since), accent)), palette,
                                       CFontSize{CFontSize::HT_FONT_H3}, 1.0F));
            words->addChild(markupLine(escapeMarkup(std::format("{}. Deactivate puts the desktop back as your config says.", listed(*active))), palette));
        } else {
            words->addChild(markupLine("<span weight=\"600\">Not active</span>", palette, CFontSize{CFontSize::HT_FONT_H3}, 1.0F));
            words->addChild(markupLine("Your desktop is exactly what your config says.", palette));
        }
        line->addChild(words);
        line->addChild(active ? button("Deactivate", 150, true, [this] { defer([this] { deactivate(); }); })
                              : button("Activate", 150, true, [this] { defer([this] { activate(); }); }));
        state->addChild(line);
        body->addChild(state);

        // The areas, two to a row, each with its own switch.
        for (std::size_t first = 0; first < activation::areas.size(); first += 2) {
            const auto row = leftRow(0);
            row->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->gap(12)->commence();
            for (std::size_t index = first; index < first + 2; ++index) {
                const auto area    = activation::areas[index];
                const auto text    = textOf(area);
                const auto inArea  = effects.in(area);
                const bool isOn    = allowed(area);
                const auto card    = panel(palette, CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {0.495, 1.0}}, isOn);
                const auto inside  = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
                inside->setMargin(14);
                const auto heading = leftRow(34);
                const auto name    = markupLine(std::format("{}  <span weight=\"600\">{}</span>", colored(text.symbol, isOn ? accent : hex(palette->m_colors.text)), escapeMarkup(text.title)),
                                                palette, CFontSize{CFontSize::HT_FONT_H3}, 1.0F);
                name->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
                name->setGrow(true, false);
                heading->addChild(name);
                if (area != activation::Area::theme) {
                    heading->addChild(button("Off", 70, !isOn, [this, area] { defer([this, area] { allow(area, false); }); }));
                    heading->addChild(button("Allowed", 100, isOn, [this, area] { defer([this, area] { allow(area, true); }); }));
                }
                inside->addChild(heading);
                inside->addChild(markupLine(escapeMarkup(std::string{text.about}), palette, CFontSize{CFontSize::HT_FONT_SMALL}, 0.7F));
                if (area != activation::Area::theme) {
                    // What it would do: the first few effects, as written.
                    const auto shown = std::min<std::size_t>(inArea.size(), 3);
                    if (inArea.empty())
                        inside->addChild(markupLine("<i>No effects in the file.</i>", palette, CFontSize{CFontSize::HT_FONT_SMALL}, 0.6F));
                    for (std::size_t each = 0; each < shown; ++each) {
                        auto first = inArea[each]->source.substr(0, inArea[each]->source.find('\n'));
                        if (first.size() > 70)
                            first = first.substr(0, 69) + "…";
                        inside->addChild(markupLine(std::format("<span font_family=\"monospace\">{}</span>", escapeMarkup(first)), palette, CFontSize{CFontSize::HT_FONT_SMALL}, 0.85F));
                    }
                    if (inArea.size() > shown)
                        inside->addChild(markupLine(std::format("and {} more", inArea.size() - shown), palette, CFontSize{CFontSize::HT_FONT_SMALL}, 0.6F));
                    if (area == activation::Area::keybindings)
                        for (const auto& collision : collisions)
                            inside->addChild(markupLine(std::format("{}  {}", colored("!", accent), escapeMarkup(collision + ": both will run.")), palette, CFontSize{CFontSize::HT_FONT_SMALL}));
                }
                // A card sized to its content leaves out the bottom margin; this puts it back.
                inside->addChild(space(4));
                card->addChild(inside);
                row->addChild(card);
            }
            body->addChild(row);
        }

        body->addChild(space(4));
        // The file, and what in it is left out.
        const auto file = activation::effectsFile(application::lanternFolder(foundation::currentEnvironment()));
        const auto where = leftRow();
        const auto path  = markupLine(std::format("Effects file: <span font_family=\"monospace\">{}</span>", escapeMarkup(shortPath(file))), palette);
        path->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        path->setGrow(true, false);
        where->addChild(path);
        if (const auto opener = desktop::findProgram("xdg-open", foundation::variable(foundation::currentEnvironment(), "PATH").value_or("")))
            where->addChild(button("Open", 90, false, [this, opener = *opener, file] {
                defer([this, opener, file] {
                    // The user's own editor, the way their desktop opens a .lua file.
                    if (const auto opened = desktop::startCommand(std::format("'{}' '{}'", opener.string(), file.string())); !opened)
                        outcome = opened.error();
                    showOverview();
                });
            }));
        where->addChild(button("Read again", 130, false, [this] {
            defer([this] {
                readEffects();
                outcome.clear();
                showOverview();
            });
        }));
        body->addChild(where);
        for (const auto& skipped : effects.skipped)
            body->addChild(markupLine(escapeMarkup(std::format("Line {} is left out: {}.", skipped.line, skipped.reason)), palette, CFontSize{CFontSize::HT_FONT_SMALL}, 0.7F));
        for (const auto& problem : problems)
            body->addChild(markupLine(std::format("{}  {}", colored("!", accent), escapeMarkup(problem)), palette, CFontSize{CFontSize::HT_FONT_SMALL}));

        CSharedPointer<Hyprtoolkit::IElement> note;
        if (!outcome.empty()) {
            const auto made = markupLine(escapeMarkup(outcome), palette);
            made->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
            note = made;
        }
        setFooter(footer, {}, note);
    }

}
