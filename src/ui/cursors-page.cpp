// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/cursors-page.hpp"

#include "application/config-check.hpp"
#include "application/lantern-folder.hpp"
#include "configuration/diff.hpp"
#include "configuration/lua-edit.hpp"
#include "configuration/main-config.hpp"
#include "desktop/clipboard.hpp"
#include "desktop/process.hpp"
#include "desktop/system.hpp"
#include "desktop/waybar.hpp"
#include "foundation/environment.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"
#include "ui/widgets.hpp"

#include <hyprtoolkit/element/Image.hpp>
#include <hyprtoolkit/element/Null.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <map>
#include <sstream>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprtoolkit::CFontSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr auto   changeName = "Cursors";
        constexpr double cardShare  = 0.24;
        constexpr int    drawnSize  = 32;

        // The cursors a card shows, each by the names themes give it.
        const std::vector<std::vector<std::string_view>> shownCursors{
            {"default", "left_ptr", "arrow"},
            {"pointer", "hand2", "hand1", "pointing_hand"},
            {"text", "xterm", "ibeam"},
            {"wait", "watch"},
        };

        // What a package's cursors are called on its card.
        std::string packageTitle(std::string_view package) {
            static const std::map<std::string_view, std::string_view> titles{
                {"breeze-cursors", "Breeze"},        {"capitaine-cursors", "Capitaine"}, {"vimix-cursors", "Vimix"},  {"xcursor-vanilla-dmz", "Vanilla DMZ"},
                {"xcursor-vanilla-dmz-aa", "Vanilla DMZ, smooth"}, {"xcursor-comix", "Comix"}, {"oxygen-cursors", "Oxygen"}, {"xcursor-themes", "X.org's glass"},
            };
            const auto found = titles.find(package);
            return std::string{found != titles.end() ? found->second : package};
        }

        CSharedPointer<Hyprtoolkit::IElement> blank(double width, double height) {
            return Hyprtoolkit::CNullBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {width, height}})->commence();
        }

    }

    CursorsPage::CursorsPage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop) :
        palette{std::move(palette)},
        defer{std::move(defer)},
        loop{std::move(loop)},
        diff{this->palette, this->defer,
             {RowList::Column{.width = 24, .monospace = true, .secondary = false, .markup = true, .heading = ""},
              RowList::Column{.width = 0, .monospace = true, .secondary = false, .markup = true, .heading = ""}},
             20} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        root->addChild(pageTitle("Cursors", this->palette));
        root->addChild(paragraph("The mouse cursor's look, for Hyprland and every app. The size is on the Accessibility page. Nothing is written until you review "
                                 "the change.",
                                 this->palette));
        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        root->addChild(body);
        footer = leftRow();
        root->addChild(footer);
        showOverview();
    }

    void CursorsPage::restyle() {
        if (!reviewing)
            showOverview();
    }

    std::expected<std::vector<configuration::FileChange>, std::string> CursorsPage::plan() const {
        const auto config = configuration::mainConfig(configuration::lanternLaunch());
        if (!config)
            return std::unexpected{config.error()};
        const auto found = std::ranges::find(themes, selected, &desktop::CursorTheme::name);
        if (found == themes.end() || found->folder.empty())
            return std::vector<configuration::FileChange>{};
        return desktop::planCursorTheme(*found, *config, foundation::currentEnvironment());
    }

    void CursorsPage::showOverview() {
        reviewing = false;
        body->clearChildren();
        cards.clear();
        statusShown.clear();
        const auto environment = foundation::currentEnvironment();
        themes                 = desktop::cursorThemes(environment);
        if (const auto config = configuration::mainConfig(configuration::lanternLaunch()))
            inUse = desktop::currentCursorTheme(*config, environment);
        if (selected.empty())
            selected = inUse;

        const auto addCard = [this](const std::string& name, const std::string& label, const CSharedPointer<Hyprtoolkit::IElement>& picture) {
            Card made{.name = name, .frame = nullptr, .title = nullptr, .label = label};
            made.frame = Hyprtoolkit::CRectangleBuilder::begin()
                             ->color([palette = palette] { return palette->m_colors.alternateBase; })
                             ->rounding(palette->m_vars.bigRounding)
                             ->borderColor([] { return Hyprtoolkit::CHyprColor{0, 0, 0, 0}; })
                             ->borderThickness(2)
                             ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {cardShare, 1.0}})
                             ->commence();
            const auto inside = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(8)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
            inside->setMargin(10);
            if (picture)
                inside->addChild(picture);
            made.title = markupLine(escapeMarkup(label), palette);
            inside->addChild(made.title);
            made.frame->addChild(inside);
            made.frame->setReceivesMouse(true);
            made.frame->setMouseButton([this, name](Hyprtoolkit::Input::eMouseButton button, bool pressed) {
                if (button != Hyprtoolkit::Input::MOUSE_BUTTON_LEFT || !pressed)
                    return;
                defer([this, name] {
                    const auto found = std::ranges::find(themes, name, &desktop::CursorTheme::name);
                    if (found != themes.end() && found->folder.empty()) {
                        installing = found->package;
                    } else {
                        installing.clear();
                        selected = name;
                    }
                    updateSelection();
                });
            });
            cards.push_back(std::move(made));
            return cards.back().frame;
        };
        const auto rows = [this](const std::vector<CSharedPointer<Hyprtoolkit::IElement>>& made) {
            for (std::size_t first = 0; first < made.size(); first += 4) {
                const auto line = leftRow(0);
                line->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->gap(12)->commence();
                for (std::size_t index = first; index < std::min(made.size(), first + 4); ++index)
                    line->addChild(made[index]);
                body->addChild(line);
            }
        };

        // The installed themes, each with its own arrow, hand, text and busy cursors.
        std::vector<CSharedPointer<Hyprtoolkit::IElement>> installed;
        for (const auto& theme : themes) {
            if (theme.folder.empty())
                continue;
            const auto picture = Hyprtoolkit::CRectangleBuilder::begin()
                                     ->color([palette = palette] { return palette->m_colors.base; })
                                     ->rounding(palette->m_vars.smallRounding)
                                     ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 64.0}})
                                     ->commence();
            const auto line = Hyprtoolkit::CRowLayoutBuilder::begin()->gap(14)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
            line->addChild(blank(10, 1));
            for (const auto& names : shownCursors) {
                auto image = desktop::cursorImage(theme.folder, names, drawnSize);
                if (!image)
                    continue;
                line->addChild(Hyprtoolkit::CImageBuilder::begin()
                                   ->data(std::move(*image))
                                   ->fitMode(Hyprtoolkit::IMAGE_FIT_MODE_CONTAIN)
                                   ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {static_cast<double>(drawnSize), static_cast<double>(drawnSize)}})
                                   ->commence());
            }
            picture->addChild(line);
            installed.push_back(addCard(theme.name, theme.title, picture));
        }
        rows(installed);

        // The distribution's, one card for each package not installed.
        std::vector<std::string>                           packages;
        std::vector<CSharedPointer<Hyprtoolkit::IElement>> offered;
        for (const auto& theme : themes)
            if (theme.folder.empty() && !std::ranges::contains(packages, theme.package)) {
                packages.push_back(theme.package);
                offered.push_back(addCard(theme.name, packageTitle(theme.package), nullptr));
            }
        if (!offered.empty()) {
            body->addChild(space(4));
            body->addChild(markupLine("<span weight=\"600\">More cursor themes</span>   " +
                                          std::format("<span size=\"small\" alpha=\"70%\">{}</span>", escapeMarkup("Not installed. Pick one to see the command that installs it.")),
                                      palette));
            rows(offered);
        }
        hint = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        body->addChild(hint);
        updateSelection();
    }

    void CursorsPage::showInstallHint() {
        hint->clearChildren();
        if (installing.empty())
            return;
        const auto environment = foundation::currentEnvironment();
        const auto install     = desktop::installCommand(desktop::Requirement{.name = installing, .purpose = "", .upstream = "", .package = installing, .found = std::nullopt},
                                                         desktop::currentDistribution());
        const auto lead        = std::format("{} is not installed. Run this, then press Check again:", escapeMarkup(packageTitle(installing)));
        const auto line        = leftRow(40);
        if (install.empty()) {
            line->addChild(markupLine(std::format("{} is not installed. On Arch Linux its package is {}.", escapeMarkup(packageTitle(installing)), escapeMarkup(installing)), palette));
        } else {
            const auto command = commandRow(install, desktop::clipboardProgram(foundation::variable(environment, "PATH").value_or("")), palette, defer, lead);
            command->setGrow(true, false);
            line->addChild(command);
        }
        line->addChild(button("Check again", 130, false, [this] {
            defer([this] {
                installing.clear();
                showOverview();
            });
        }));
        hint->addChild(space(4));
        hint->addChild(line);
    }

    void CursorsPage::updateSelection() {
        for (auto& made : cards) {
            const bool chosen = made.name == selected || (!installing.empty() && std::ranges::any_of(themes, [&](const auto& theme) {
                                                              return theme.name == made.name && theme.folder.empty() && theme.package == installing;
                                                          }));
            made.frame->rebuild()->borderColor([palette = palette, chosen] { return chosen ? palette->m_colors.accent : Hyprtoolkit::CHyprColor{0, 0, 0, 0}; })->commence();
            auto label = escapeMarkup(made.label);
            if (chosen)
                label = std::format("<span weight=\"600\">{}</span>", label);
            if (made.name == inUse)
                label += std::format("  {}", colored("· in use", hex(palette->m_colors.accent)));
            made.title->rebuild()->text(std::move(label))->commence();
        }
        showInstallHint();
        fillStatus();
    }

    void CursorsPage::fillStatus() {
        const auto environment = foundation::currentEnvironment();
        const auto planned     = plan();
        const auto found       = std::ranges::find(themes, selected, &desktop::CursorTheme::name);
        const bool gtkSame     = found == themes.end() || found->folder.empty() || desktop::cursorGsettings(*found, environment).empty();
        const bool undoable    = configuration::latestChange(application::backupsFolder(environment), changeName).has_value();
        // The theme in use needs nothing written, even where the config leaves it to a default.
        const bool same        = planned && (selected == inUse || (planned->empty() && gtkSame));
        const auto shown       = std::format("{}|{}|{}|{}|{}", selected, same, planned ? "" : planned.error(), undoable, outcome);
        if (shown == statusShown)
            return;
        statusShown = shown;

        // Everything said here sits in the footer, so that saying it moves nothing above.
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> buttons;
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> away;
        if (undoable)
            away.push_back(button("Undo last change", 160, false, [this] { defer([this] { undo(); }); }));
        std::string note;
        if (!planned) {
            note = escapeMarkup(std::format("Lantern cannot apply it: {}.", planned.error()));
        } else if (same) {
            const auto title = found != themes.end() ? found->title : selected;
            note             = std::format("{}  {}", colored("✓", hex(palette->m_colors.accent)), escapeMarkup(outcome.empty() ? std::format("Your cursor is {}.", title) : outcome));
        } else {
            buttons.push_back(button("Review changes", 160, true, [this] { defer([this] { showReview(); }); }));
            note = escapeMarkup(outcome);
        }
        CSharedPointer<Hyprtoolkit::IElement> noteText;
        if (!note.empty()) {
            const auto made = markupLine(note, palette);
            made->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
            noteText = made;
        }
        setFooter(footer, buttons, noteText, away);
    }

    void CursorsPage::showReview() {
        auto       planned     = plan();
        const auto environment = foundation::currentEnvironment();
        const auto found       = std::ranges::find(themes, selected, &desktop::CursorTheme::name);
        if (!planned || found == themes.end()) {
            outcome = planned ? "" : planned.error();
            return showOverview();
        }
        reviewing = true;
        changes   = std::move(*planned);
        gtk       = desktop::cursorGsettings(*found, environment);
        outcome.clear();
        body->clearChildren();
        body->addChild(space(4));
        body->addChild(heading(std::format("Review: {} cursors", found->title), palette));
        body->addChild(paragraph("Applying will:", palette));
        if (!changes.empty())
            body->addChild(paragraph("•  Write the changes below. A copy of each file is kept first, so it can be undone.", palette, true));
        for (const auto& setting : gtk)
            body->addChild(paragraph(std::format("•  Set GTK's {} from {} to {}, for GTK apps.", setting.key, setting.current, setting.wanted), palette, true));
        body->addChild(paragraph("•  Show the new cursor at once. Apps that draw their own keep the old one until they are opened again.", palette, true));
        setFooter(footer, {button("Cancel", 110, false, [this] { defer([this] { showOverview(); }); }), button("Apply", 110, true, [this] { defer([this] { apply(); }); })});
        diff.setRows(diffRows(changes, *palette));
        body->addChild(diff.element());
    }

    void CursorsPage::apply() {
        if (!reviewing)
            return;
        const auto environment = foundation::currentEnvironment();
        const auto record      = configuration::applyChanges(changes, application::backupsFolder(environment), std::format("{}: {}", changeName, selected));
        if (!record) {
            outcome = std::format("Nothing was written: {}.", record.error());
            return showOverview();
        }
        // GTK's settings live in dconf: their old values go beside the change's record, for Undo.
        desktop::setGsettings(gtk, *record, environment);
        outcome = "Applied.";
        const auto hyprctl = desktop::findProgram("hyprctl", foundation::variable(environment, "PATH").value_or(""));
        if (hyprctl) {
            // The cursor now, at the size the config sets.
            std::string size = "24";
            if (const auto config = configuration::mainConfig(configuration::lanternLaunch()); config && config->lua()) {
                std::ifstream     stream{config->file};
                std::stringstream text;
                text << stream.rdbuf();
                size = configuration::luaEnv(text.str(), "XCURSOR_SIZE").value_or(size);
            }
            (void)desktop::readCommand(*hyprctl, {"setcursor", selected, size});
            if (!changes.empty())
                application::checkHyprlandConfig(loop, *hyprctl, *record, [this](std::optional<std::string> problem) {
                    if (!problem)
                        return;
                    outcome = *problem;
                    if (!reviewing)
                        showOverview();
                });
        }
        showOverview();
    }

    void CursorsPage::undo() {
        const auto environment = foundation::currentEnvironment();
        const auto latest      = configuration::latestChange(application::backupsFolder(environment), changeName);
        if (!latest)
            return showOverview();
        if (const auto undone = configuration::undoChange(*latest); !undone) {
            outcome = std::format("Nothing was undone: {}.", undone.error());
        } else {
            desktop::restoreGsettings(*latest, environment);
            outcome  = "Undone.";
            selected.clear();
            if (const auto config = configuration::mainConfig(configuration::lanternLaunch()))
                if (const auto hyprctl = desktop::findProgram("hyprctl", foundation::variable(environment, "PATH").value_or("")))
                    (void)desktop::readCommand(*hyprctl, {"setcursor", desktop::currentCursorTheme(*config, environment), "24"});
        }
        showOverview();
    }

}
