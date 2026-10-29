// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/theme-page.hpp"

#include "application/command.hpp"
#include "application/config-check.hpp"
#include "application/lantern-folder.hpp"
#include "configuration/main-config.hpp"
#include "desktop/process.hpp"
#include "desktop/system.hpp"
#include "desktop/theme.hpp"
#include "desktop/window-look.hpp"
#include "ui/bar-preview.hpp"
#include "ui/desktop-preview.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"
#include "ui/typing.hpp"
#include "ui/widgets.hpp"

#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Textbox.hpp>

#include <algorithm>
#include <csignal>
#include <fstream>
#include <format>
#include <numbers>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprtoolkit::CFontSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr auto   changeName = "Theme";
        constexpr double cardShare  = 0.32;

        std::filesystem::path themesFolder() {
            return application::lanternFolder(foundation::currentEnvironment()) / "themes";
        }

        std::optional<std::filesystem::path> program(std::string_view name) {
            return desktop::findProgram(name, foundation::variable(foundation::currentEnvironment(), "PATH").value_or(""));
        }

        // A draft's colors as a palette, its views into the draft.
        desktop::Palette paletteOf(const desktop::ThemeDraft& draft, const std::string& accent) {
            return desktop::Palette{.name = draft.name, .dark = draft.dark, .base = draft.base, .onAccent = draft.onAccent, .surface = draft.surface, .raised = draft.raised,
                                    .muted = draft.muted, .subtle = draft.subtle, .text = draft.text, .accent = draft.accent, .audio = draft.audio, .network = draft.network,
                                    .alert = draft.alert, .gnomeAccent = accent};
        }

    }

    ThemePage::ThemePage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop) :
        palette{std::move(palette)},
        defer{std::move(defer)},
        loop{std::move(loop)},
        diff{this->palette, this->defer,
             {RowList::Column{.width = 24, .monospace = true, .secondary = false, .markup = true, .heading = ""},
              RowList::Column{.width = 0, .monospace = true, .secondary = false, .markup = true, .heading = ""}},
             20},
        waybar{this->palette, this->defer, this->loop, "waybar", "Waybar", nullptr} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        root->addChild(pageTitle("Theme", this->palette));
        root->addChild(paragraph("One palette for the desktop: the borders of your windows, Waybar, the launcher, notifications, GTK apps and the apps built "
                                 "on hyprtoolkit, Lantern among them. The terminal has themes of its own, on the Terminal page. Nothing is written until "
                                 "you review the change.",
                                 this->palette));
        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        // A page laid out taller than the window comes out wrong, so the large preview takes
        // only the height the cards leave, measured after each layout pass.
        body->setRepositioned([this] { this->defer([this] { fitPreview(); }); });
        root->addChild(body);
        footer = leftRow();
        root->addChild(footer);

        const auto config = configuration::mainConfig(configuration::lanternLaunch());
        selected          = config ? desktop::currentTheme(*config) : nullptr;
        if (selected == nullptr)
            if (const auto style = desktop::currentBarStyle(foundation::currentEnvironment(), desktop::runningBarGeometry()))
                selected = style->palette;
        if (selected == nullptr)
            selected = &desktop::waybarPalettes().front();
        showOverview();
    }

    void ThemePage::close() {
        waybar.close();
    }

    CSharedPointer<Hyprtoolkit::IElement> ThemePage::desktopOf(const desktop::Palette* shown, bool large) {
        auto& made = desktops[{shown, large}];
        if (made)
            return made;
        // The large one is drawn smaller when the page has less height to give it.
        const double scale  = large ? previewHeight / 250 : 0.42;
        const double height = large ? previewHeight : 105;
        // The windows as the config sets them, so this and the Windows page show the same desktop.
        const auto bar    = desktop::currentBarStyle(foundation::currentEnvironment(), desktop::runningBarGeometry()).value_or(desktop::WaybarStyle{desktop::Shape::islands, shown});
        const auto config = configuration::mainConfig(configuration::lanternLaunch());
        const auto look   = (config ? desktop::currentWindowLook(*config) : std::nullopt).value_or(desktop::WindowLook{});
        made              = desktopPreview(*shown, look, bar, scale, height);
        return made;
    }

    void ThemePage::showOverview() {
        reviewing = false;
        editing   = false;
        changes.clear();
        body->clearChildren();

        largePreview = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        statusShown.clear();
        body->addChild(largePreview);

        cards.clear();
        cardRows.clear();
        const auto all = desktop::allPalettes();
        for (std::size_t first = 0; first < all.size(); first += 3) {
            const auto line = leftRow(0);
            line->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->gap(12)->commence();
            for (std::size_t index = first; index < std::min(all.size(), first + 3); ++index) {
                Card made;
                made.frame = Hyprtoolkit::CRectangleBuilder::begin()
                                 ->color([palette = palette] { return palette->m_colors.alternateBase; })
                                 ->rounding(palette->m_vars.bigRounding)
                                 ->borderColor([] { return Hyprtoolkit::CHyprColor{0, 0, 0, 0}; })
                                 ->borderThickness(2)
                                 ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {cardShare, 1.0}})
                                 ->commence();
                const auto inside = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(8)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
                inside->setMargin(10);
                made.holder = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
                made.holder->addChild(desktopOf(all[index], false));
                made.name = markupLine(escapeMarkup(all[index]->name), palette);
                inside->addChild(made.holder);
                inside->addChild(made.name);
                made.frame->addChild(inside);
                made.frame->setReceivesMouse(true);
                made.frame->setMouseButton([this, chosen = all[index]](Hyprtoolkit::Input::eMouseButton button, bool pressed) {
                    if (button != Hyprtoolkit::Input::MOUSE_BUTTON_LEFT || !pressed)
                        return;
                    defer([this, chosen] {
                        selected = chosen;
                        updateSelection();
                    });
                });
                line->addChild(made.frame);
                cards.emplace_back(all[index], std::move(made));
            }
            body->addChild(line);
            cardRows.push_back(line);
        }
        // A theme of your own, started from the one chosen, and the chosen one's editor when
        // it is yours.
        const auto actions = leftRow();
        actions->rebuild()->gap(8)->commence();
        actions->addChild(button("New theme from this", 210, false, [this] { defer([this] { showEditor(*selected, std::nullopt); }); }));
        if (const auto file = desktop::userThemeFile(selected))
            actions->addChild(button("Edit", 90, false, [this, file = *file] { defer([this, file] { showEditor(*selected, file); }); }));
        body->addChild(actions);
        cardRows.push_back(actions);
        updateSelection();
    }

    void ThemePage::showEditor(const desktop::Palette& from, std::optional<std::filesystem::path> file) {
        editing   = true;
        draft     = desktop::draftOf(from);
        draftFile = std::move(file);
        if (!draftFile)
            draft.name = std::format("{} (mine)", from.name);
        draftAccent = std::string{desktop::gnomeAccentOf(draft.accent)};
        cards.clear();
        cardRows.clear();
        statusShown.clear();
        body->clearChildren();
        body->addChild(heading(draftFile ? std::format("Edit: {}", from.name) : "A theme of your own", palette));

        draftPreview = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        body->addChild(draftPreview);

        // The name and the kind line up with the colors' values below.
        const auto nameRow = leftRow(scaled(40));
        nameRow->rebuild()->gap(10)->commence();
        const auto label = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {scaled(220), 1.0}})->commence();
        label->addChild(markupLine("<span weight=\"600\">Name</span>", palette));
        nameRow->addChild(label);
        const auto name = Hyprtoolkit::CTextboxBuilder::begin()
                              ->defaultText(std::string{draft.name})
                              ->onTextEdited([this](CSharedPointer<Hyprtoolkit::CTextboxElement>, const std::string& text) {
                                  draft.name = text;
                                  this->defer([this] { updateEditor(); });
                              })
                              ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {scaled(300), scaled(34)}})
                              ->commence();
        registerTextbox(name);
        nameRow->addChild(name);
        body->addChild(nameRow);
        body->addChild(settingRow("Kind", "for GTK apps", {"Dark", "Light"}, draft.dark ? 0 : 1, palette,
                                  [this](std::size_t chosen) {
                                      draft.dark = chosen == 0;
                                      updateEditor();
                                  },
                                  defer));

        // The colors two to a row: a swatch, the name and what it colors, and its value.
        swatches.clear();
        const auto colors = desktop::draftColors();
        const bool picker  = program("hyprpicker").has_value();
        for (std::size_t first = 0; first < colors.size(); first += 2) {
            const auto line = leftRow(0);
            line->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->gap(24)->commence();
            for (std::size_t index = first; index < std::min(colors.size(), first + 2); ++index) {
                const auto& color = colors[index];
                const auto  side   = leftRow(scaled(40));
                side->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {0.49, scaled(40)}})->gap(10)->commence();
                const auto swatch = Hyprtoolkit::CRectangleBuilder::begin()
                                        ->color([value = paletteColor(draft.*color.member)] { return value; })
                                        ->rounding(6)
                                        ->borderColor([palette = palette] { return palette->m_colors.alternateBase; })
                                        ->borderThickness(1)
                                        ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {scaled(28), scaled(28)}})
                                        ->commence();
                swatches.push_back(swatch);
                side->addChild(swatch);
                const auto names = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {scaled(220), 1.0}})->commence();
                names->addChild(markupLine(std::format("<span weight=\"600\">{}</span>", escapeMarkup(std::string{color.name})), palette));
                names->addChild(markupLine(escapeMarkup(std::string{color.about}), palette, CFontSize{CFontSize::HT_FONT_SMALL}, 0.6F));
                side->addChild(names);
                const auto value = Hyprtoolkit::CTextboxBuilder::begin()
                                       ->defaultText(std::string{draft.*color.member})
                                       ->onTextEdited([this, member = color.member](CSharedPointer<Hyprtoolkit::CTextboxElement>, const std::string& text) {
                                           draft.*member = text;
                                           this->defer([this] { updateEditor(); });
                                       })
                                       ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {scaled(110), scaled(34)}})
                                       ->commence();
                registerTextbox(value);
                side->addChild(value);
                if (picker)
                    side->addChild(button("Pick", 70, false, [this, member = color.member, value] {
                        defer([this, member, value] {
                            const auto hyprpicker = program("hyprpicker");
                            if (!hyprpicker)
                                return;
                            // hyprpicker lets you click anywhere on the screens and says the color there.
                            (void)application::runCommand(loop, *hyprpicker, {"--format=hex", "--no-fancy"}, [this, member, value](bool ok, std::string output) {
                                const auto at = output.find('#');
                                if (!ok || at == std::string::npos || output.size() < at + 7 || !editing)
                                    return;
                                draft.*member = output.substr(at, 7);
                                value->setText(draft.*member);
                                updateEditor();
                            });
                        });
                    }));
                line->addChild(side);
            }
            body->addChild(line);
        }
        updateEditor();
    }

    void ThemePage::updateEditor() {
        if (!editing)
            return;
        draftAccent        = std::string{desktop::gnomeAccentOf(draft.accent)};
        const auto shown   = paletteOf(draft, draftAccent);
        const auto bar     = desktop::currentBarStyle(foundation::currentEnvironment(), desktop::runningBarGeometry()).value_or(desktop::WaybarStyle{desktop::Shape::islands, &shown});
        const auto config  = configuration::mainConfig(configuration::lanternLaunch());
        const auto look    = (config ? desktop::currentWindowLook(*config) : std::nullopt).value_or(desktop::WindowLook{});
        auto       drawn   = bar;
        drawn.palette      = &shown;
        draftPreview->clearChildren();
        draftPreview->addChild(desktopPreview(shown, look, drawn, 170.0 / 250, 170));
        const auto colors = desktop::draftColors();
        for (std::size_t index = 0; index < swatches.size() && index < colors.size(); ++index)
            swatches[index]->rebuild()->color([value = paletteColor(draft.*colors[index].member)] { return value; })->commence();

        const auto problems = desktop::draftProblems(draft);
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> buttons{button("Cancel", 110, false, [this] { defer([this] { showOverview(); }); })};
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> away;
        if (draftFile)
            away.push_back(button("Delete this theme", 180, false, [this] { defer([this] { deleteDraft(); }); }));
        // A theme whose text cannot be read can still be saved; one with a color that is not
        // a color cannot.
        const bool savable = std::ranges::none_of(problems, [](const std::string& problem) { return problem.contains("#rrggbb") || problem.contains("name"); });
        if (savable)
            buttons.push_back(button("Save", 110, true, [this] { defer([this] { saveDraft(); }); }));
        const auto note = markupLine(escapeMarkup(problems.empty() ? std::string{"Save keeps it among the themes, in Lantern's folder; then review it to use it."} : problems.front()), palette);
        note->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        setFooter(footer, buttons, note, away);
    }

    void ThemePage::saveDraft() {
        const auto folder = themesFolder();
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        const auto file   = draftFile.value_or(desktop::newThemeFile(folder, draft.name));
        if (const auto written = configuration::writeWhole(file, desktop::themeFileText(draft)); !written) {
            outcome = std::format("The theme was not saved: {}.", written.error());
            return showOverview();
        }
        desktop::loadUserPalettes(folder);
        for (const auto* one : desktop::allPalettes())
            if (desktop::userThemeFile(one) == file)
                selected = one;
        outcome = "Saved.";
        showOverview();
    }

    void ThemePage::deleteDraft() {
        if (!draftFile)
            return showOverview();
        std::error_code error;
        std::filesystem::remove(*draftFile, error);
        desktop::loadUserPalettes(themesFolder());
        if (desktop::userThemeFile(selected) == draftFile || !std::ranges::contains(desktop::allPalettes(), selected)) {
            const auto config = configuration::mainConfig(configuration::lanternLaunch());
            selected          = config ? desktop::currentTheme(*config) : nullptr;
            if (selected == nullptr)
                selected = desktop::allPalettes().front();
        }
        outcome = error ? std::format("The theme was not deleted: {}.", error.message()) : "Deleted.";
        showOverview();
    }

    void ThemePage::updateSelection() {
        largePreview->clearChildren();
        if (previewHeight > 0)
            largePreview->addChild(desktopOf(selected, true));
        fillStatus();
        for (auto& [shown, made] : cards) {
            const bool chosen = shown == selected;
            made.frame->rebuild()->borderColor([palette = palette, chosen] { return chosen ? palette->m_colors.accent : Hyprtoolkit::CHyprColor{0, 0, 0, 0}; })->commence();
            auto name = escapeMarkup(shown->name);
            if (chosen)
                name = std::format("<span weight=\"600\">{}</span>", name);
            if (shown == inUse)
                name += std::format("   {}", colored("· in use", hex(palette->m_colors.accent)));
            made.name->rebuild()->text(std::string{name})->commence();
        }
    }

    void ThemePage::fillStatus() {
        const auto environment = foundation::currentEnvironment();
        const auto config      = configuration::mainConfig(configuration::lanternLaunch());
        const auto bar         = desktop::currentBarStyle(environment, desktop::runningBarGeometry()).value_or(desktop::WaybarStyle{desktop::Shape::islands, selected});
        const auto planned     = config ? desktop::planTheme(*selected, bar, *config, environment)
                                        : std::expected<std::vector<configuration::FileChange>, std::string>{std::unexpect, config.error()};
        const bool undoable    = configuration::latestChange(application::backupsFolder(environment), changeName).has_value();
        const bool gtkSame     = desktop::gtkChanges(*selected, environment).empty();
        inUse                  = config ? desktop::currentTheme(*config) : nullptr;
        const auto shown = std::format("{}|{}|{}|{}|{}|{}", selected->name, planned ? planned->empty() : false, gtkSame, planned ? "" : planned.error(), undoable, outcome);
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
        } else if (planned->empty() && gtkSame) {
            note = std::format("{}  Your desktop uses {}.", colored("✓", hex(palette->m_colors.accent)), escapeMarkup(selected->name));
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

    void ThemePage::showReview() {
        const auto environment = foundation::currentEnvironment();
        const auto config      = configuration::mainConfig(configuration::lanternLaunch());
        const auto bar         = desktop::currentBarStyle(environment, desktop::runningBarGeometry()).value_or(desktop::WaybarStyle{desktop::Shape::islands, selected});
        auto       planned     = config ? desktop::planTheme(*selected, bar, *config, environment)
                                        : std::expected<std::vector<configuration::FileChange>, std::string>{std::unexpect, config.error()};
        if (!planned) {
            outcome = planned.error();
            return showOverview();
        }
        reviewing = true;
        changes   = std::move(*planned);
        outcome.clear();
        body->clearChildren();
        body->addChild(space(4));
        body->addChild(heading(std::format("Review: {}", selected->name), palette));
        gtk = desktop::gtkChanges(*selected, environment);
        body->addChild(paragraph("Applying will:", palette));
        std::vector<std::string> steps;
        if (!changes.empty())
            steps.emplace_back("Write the changes below. A copy of each file is kept first, so it can be undone.");
        for (const auto& setting : gtk)
            steps.push_back(std::format("Set GTK's {} from {} to {}, for GTK apps.", setting.key, setting.current, setting.wanted));
        steps.emplace_back("Show the new colors at once: Hyprland's borders, Lantern, Waybar, the launcher and dunst's notifications.");
        if (std::ranges::any_of(changes, [](const auto& change) { return change.file.filename() == "gtk.css"; }))
            steps.emplace_back("Give GTK apps, such as the volume control and the network settings, the palette too: every one opened from now on, "
                               "and the rest once they are opened again.");
        for (const auto& step : steps)
            body->addChild(paragraph(std::format("•  {}", step), palette, true));
        setFooter(footer, {button("Cancel", 110, false, [this] { defer([this] { showOverview(); }); }), button("Apply", 110, true, [this] { defer([this] { apply(); }); })});
        if (!changes.empty()) {
            diff.setRows(diffRows(changes, *palette));
            body->addChild(diff.element());
        }
    }

    void ThemePage::apply() {
        if (!reviewing)
            return;
        const auto environment = foundation::currentEnvironment();
        const auto record      = configuration::applyChanges(changes, application::backupsFolder(environment), std::format("{}: {}", changeName, selected->name));
        if (!record) {
            outcome = std::format("Nothing was written: {}.", record.error());
            return showOverview();
        }
        // GTK's settings live in dconf, not in a file: their old values go beside the change's
        // record, for Undo, before they are set.
        desktop::setGsettings(gtk, *record, environment);
        reloadApps();

        const auto changed = [this](auto test) { return std::ranges::any_of(changes, test); };
        const bool toolkit = changed([](const auto& change) { return change.file.filename() == "hyprtoolkit.conf"; });
        if (toolkit) {
            // Lantern's own colors, set before hyprtoolkit sees the file change and
            // redraws every element with them.
            auto& colors          = palette->m_colors;
            colors.background     = paletteColor(selected->base);
            colors.base           = paletteColor(selected->surface);
            colors.alternateBase  = paletteColor(selected->raised);
            colors.text           = paletteColor(selected->text);
            colors.brightText     = paletteColor(selected->text);
            colors.linkText       = paletteColor(selected->audio);
            colors.accent         = paletteColor(selected->accent);
            colors.accentSecondary = paletteColor(selected->network);
            // Every element is made again in the new colors.
            if (recolored)
                defer(recolored);
        }
        const bool bar     = changed([](const auto& change) { return change.file.parent_path().filename() == "waybar"; });
        const bool config  = changed([](const auto& change) { return change.file.extension() == ".lua"; });
        const auto hyprctl = desktop::findProgram("hyprctl", foundation::variable(environment, "PATH").value_or(""));

        // Waybar comes back and Hyprland checks the config while Lantern keeps running;
        // a problem with either is reported here.
        auto waiting  = std::make_shared<int>((bar ? 1 : 0) + (config && hyprctl ? 1 : 0));
        auto problem  = std::make_shared<std::optional<std::string>>();
        auto finished = [this, waiting, problem] {
            if (--*waiting > 0)
                return;
            if (*problem) {
                outcome = **problem;
                if (!reviewing)
                    showOverview();
            }
        };
        outcome = std::format("Applied {}.", selected->name);
        if (bar)
            waybar.restartToRead(*record, [problem, finished](std::string what) {
                if (what.contains("put back"))
                    *problem = what;
                finished();
            });
        if (config && hyprctl)
            application::checkHyprlandConfig(loop, *hyprctl, *record, [problem, finished](std::optional<std::string> what) {
                if (what)
                    *problem = what;
                finished();
            });
        if (*waiting == 0) {
            ++*waiting;
            finished();
        }
        showOverview();
    }

    void ThemePage::undo() {
        const auto environment = foundation::currentEnvironment();
        const auto latest      = configuration::latestChange(application::backupsFolder(environment), changeName);
        if (!latest)
            return showOverview();
        if (const auto undone = configuration::undoChange(*latest); !undone) {
            outcome = std::format("Nothing was undone: {}.", undone.error());
        } else {
            // GTK's settings as they were before the change.
            desktop::restoreGsettings(*latest, environment);
            reloadApps();
            outcome = "Undone.";
            waybar.restart();
        }
        showOverview();
    }

    void ThemePage::fitPreview() {
        if (reviewing || editing || cardRows.empty())
            return;
        constexpr double full    = 250;
        // Less than this and the drawing's contents no longer fit in it.
        constexpr double least   = 80;
        constexpr double between = 10;
        double           cards   = 0;
        for (const auto& row : cardRows)
            cards += row->size().y + between;
        auto fitting = std::min(full, body->size().y - cards - between);
        if (fitting < least)
            fitting = 0;
        // Small differences are left, so a pass that changes nothing ends the fitting.
        if (std::abs(fitting - previewHeight) < 4)
            return;
        previewHeight = fitting;
        for (auto at = desktops.begin(); at != desktops.end();)
            at = at->first.second ? desktops.erase(at) : std::next(at);
        updateSelection();
    }

    void ThemePage::restyle() {
        desktops.clear();
        if (!reviewing && !editing)
            showOverview();
    }

    void ThemePage::reloadApps() {
        const auto environment = foundation::currentEnvironment();
        const auto path        = foundation::variable(environment, "PATH").value_or("");
        // dunst reads its files again on request.
        if (!desktop::findProcesses("dunst").empty())
            if (const auto dunstctl = desktop::findProgram("dunstctl", path))
                (void)desktop::readCommand(*dunstctl, {"reload"});
    }

}
