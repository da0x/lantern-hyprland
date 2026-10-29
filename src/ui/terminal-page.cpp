// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/terminal-page.hpp"

#include "application/config-check.hpp"
#include "application/settings.hpp"
#include "application/lantern-folder.hpp"
#include "configuration/main-config.hpp"
#include "desktop/accessibility.hpp"
#include "desktop/clipboard.hpp"
#include "desktop/process.hpp"
#include "desktop/system.hpp"
#include "desktop/wallpaper.hpp"
#include "foundation/environment.hpp"
#include "ui/bar-preview.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"
#include "ui/toolkit.hpp"
#include "ui/widgets.hpp"

#include <hyprtoolkit/element/Combobox.hpp>
#include <hyprtoolkit/element/Image.hpp>
#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/ScrollArea.hpp>

#include <algorithm>
#include <csignal>
#include <format>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprtoolkit::CFontSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr auto        changeName = "Terminal";

        // The Hyprland config, for the rule that keeps a Clear terminal unblurred.
        std::optional<configuration::MainConfig> hyprlandConfig() {
            auto config = configuration::mainConfig(configuration::lanternLaunch());
            return config ? std::optional{std::move(*config)} : std::nullopt;
        }

        constexpr std::size_t perRow     = 4;
        // The desktop's image around the large preview.
        constexpr double      desktopMargin = 18;
        constexpr std::size_t perPage    = 2 * perRow;
        constexpr double      cardShare  = 0.24;
        // A card: the session, its name, and the margins and gap around them.
        double cardHeight() {
            return 82 + 2 * 8 + 6 + scaled(18);
        }

        // A theme's name with what was adjusted, as in "Nord, high contrast".
        std::string fullName(const desktop::TerminalTheme& theme) {
            const auto adjustment = desktop::adjustmentName(theme.contrast, theme.vision);
            return adjustment.empty() ? theme.name : std::format("{}, {}", theme.name, adjustment);
        }

    }

    TerminalPage::TerminalPage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop) :
        palette{std::move(palette)},
        defer{std::move(defer)},
        loop{std::move(loop)},
        diff{this->palette, this->defer,
             {RowList::Column{.width = 24, .monospace = true, .secondary = false, .markup = true, .heading = ""},
              RowList::Column{.width = 0, .monospace = true, .secondary = false, .markup = true, .heading = ""}},
             20} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        title = pageTitle("Terminal", this->palette);
        root->addChild(title);
        root->addChild(paragraph("Color schemes for kitty, chosen apart from the desktop's: terminal themes are their own family, and many are best dark "
                                 "whatever the desktop is. They come from kitty's own collection, kitty-themes. Nothing is written until you review the change.",
                                 this->palette));
        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        root->addChild(body);
        footer = leftRow();
        root->addChild(footer);

        const auto environment = foundation::currentEnvironment();
        fontSize               = desktop::kittyFontSize(environment);
        fontFamily             = desktop::kittyFontFamily(environment);
        opacity                = desktop::kittyOpacity(environment);
        wallpaperImage         = desktop::shownWallpaperImage(environment);
        fonts                  = desktop::terminalFonts();
        // The adjustments are those chosen on the Accessibility page; before any were, those
        // of the theme in use, so the page opens on what kitty shows.
        selected               = desktop::currentTerminal(environment);
        if (selected != nullptr) {
            contrast = selected->contrast;
            vision   = selected->vision;
        }
        const auto settings = application::loadSettings(application::lanternFolder(environment));
        contrast            = desktop::contrastNamed(settings.contrast).value_or(contrast);
        vision              = desktop::visionNamed(settings.vision).value_or(vision);
        if (selected == nullptr || selected->contrast != contrast || selected->vision != vision) {
            const auto bundled = desktop::terminalThemes();
            const auto found   = selected != nullptr ? std::ranges::find(bundled, selected->name, &desktop::TerminalTheme::name) : bundled.end();
            selected           = &themes()[found != bundled.end() ? static_cast<std::size_t>(found - bundled.begin()) : 0];
        }
        kind = selected->dark ? Kind::dark : Kind::light;
        const auto shown = shownThemes();
        if (const auto at = std::ranges::find(shown, selected); at != shown.end())
            pageStart = static_cast<std::size_t>(at - shown.begin()) / perRow * perRow;
        showOverview();
    }

    std::optional<std::string> TerminalPage::usableFamily() const {
        if (!fontFamily)
            return std::nullopt;
        const auto found = std::ranges::find(fonts, *fontFamily, &desktop::TerminalFont::family);
        return found != fonts.end() && found->installed ? fontFamily : std::nullopt;
    }

    void TerminalPage::showFontHint() {
        fontHint->clearChildren();
        if (!fontFamily || usableFamily())
            return;
        const auto found = std::ranges::find(fonts, *fontFamily, &desktop::TerminalFont::family);
        if (found == fonts.end())
            return;
        const auto environment = foundation::currentEnvironment();
        const auto install     = desktop::installCommand(desktop::Requirement{.name = found->family, .purpose = "", .upstream = "", .package = found->package, .found = std::nullopt},
                                                         desktop::currentDistribution());
        const auto lead        = std::format("{} ({}) is not installed. Run this, then press Check again:", escapeMarkup(found->family), escapeMarkup(found->license));
        const auto line        = leftRow(40);
        if (install.empty()) {
            line->addChild(markupLine(std::format("{} Install it from your distribution's packages; on Arch Linux it is {}.", lead, escapeMarkup(found->package)), palette));
        } else {
            const auto command = commandRow(install, desktop::clipboardProgram(foundation::variable(environment, "PATH").value_or("")), palette, defer, lead);
            command->setGrow(true, false);
            line->addChild(command);
        }
        line->addChild(button("Check again", 130, false, [this] {
            defer([this] {
                fonts = desktop::terminalFonts();
                showOverview();
            });
        }));
        fontHint->addChild(line);
    }

    void TerminalPage::restyle() {
        if (!reviewing)
            showOverview();
    }

    std::span<const desktop::TerminalTheme> TerminalPage::themes() const {
        return desktop::terminalThemes(contrast, vision);
    }

    std::vector<const desktop::TerminalTheme*> TerminalPage::shownThemes() const {
        std::vector<const desktop::TerminalTheme*> shown;
        for (const auto& theme : themes())
            if (theme.dark == (kind == Kind::dark))
                shown.push_back(&theme);
        return shown;
    }

    void TerminalPage::adjust(desktop::Contrast wantedContrast, desktop::Vision wantedVision) {
        // The same theme in the new set: every set holds the bundled themes in one order.
        const auto index = static_cast<std::size_t>(selected - themes().data());
        contrast         = wantedContrast;
        vision           = wantedVision;
        selected         = &themes()[index];
        if (!reviewing)
            showOverview();
    }

    CSharedPointer<Hyprtoolkit::IElement> TerminalPage::sessionOf(const desktop::TerminalTheme* theme, bool large) {
        // The large one is drawn at the font size chosen, so it shows what kitty will.
        const auto family = large ? usableFamily().value_or("monospace") : std::string{"monospace"};
        auto&      made   = sessions[{theme, large, large ? fontSize : 0.0, family, large ? opacity : 1.0}];
        if (made)
            return made;
        const auto& color = theme->colors;
        // The large one is see-through as kitty will be, over the desktop's own image.
        const auto  frame  = Hyprtoolkit::CRectangleBuilder::begin()
                               ->color([c = paletteColor(theme->background, large ? static_cast<float>(opacity) : 1.F)] { return c; })
                               ->rounding(large ? 10 : 6)
                               ->borderColor([c = paletteColor(color[8])] { return c; })
                               ->borderThickness(1)
                               ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, large ? 200.0 : 82.0}})
                               ->commence();
        const auto lines = Hyprtoolkit::CColumnLayoutBuilder::begin()
                               ->gap(large ? 4 : 1)
                               ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})
                               ->commence();
        lines->setMargin(large ? 16 : 7);
        // A short session, colored as a shell and the usual tools color their output.
        const auto in     = [](const std::string& hex, std::string_view text) { return colored(text, hex); };
        const auto prompt = std::format("{} {} {} ", in(color[2], "you@hyprland"), in(color[4], "~/lantern-hyprland"), in(theme->foreground, "$"));
        const std::vector<std::string> session{
            prompt + in(theme->foreground, "ls"),
            std::format("{}  {}  {}  {}  {}", in(color[4], "build"), in(color[4], "docs"), in(color[4], "src"), in(theme->foreground, "README.md"), in(color[2], "configure")),
            std::format("{} {}", in(color[3], "warning:"), in(theme->foreground, "unused variable ") + in(color[5], "'scale'")),
            std::format("{} {}", in(color[1], "error:"), in(theme->foreground, "expected ';' before ") + in(color[6], "'}'")),
            in(color[8], "# 81 tests, 80 passed"),
            prompt + in(theme->cursor, "█"),
        };
        for (std::size_t index = 0; index < session.size(); ++index) {
            if (!large && index == 4)
                continue;
            lines->addChild(Hyprtoolkit::CTextBuilder::begin()
                                ->text(std::format("<span font_family=\"{}\">{}</span>", escapeMarkup(family), session[index]))
                                ->fontSize(CFontSize{CFontSize::HT_FONT_ABSOLUTE, large ? static_cast<float>(fontSize) : 5.F})
                                ->color([c = paletteColor(theme->foreground)] { return c; })
                                ->align(Hyprtoolkit::HT_FONT_ALIGN_LEFT)
                                ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})
                                ->commence());
        }
        frame->addChild(lines);
        made = frame;
        if (large && wallpaperImage) {
            const auto desktop = Hyprtoolkit::CImageBuilder::begin()
                                     ->path(wallpaperImage->string())
                                     ->fitMode(Hyprtoolkit::IMAGE_FIT_MODE_COVER)
                                     ->rounding(10)
                                     ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 200.0 + 2 * desktopMargin}})
                                     ->commence();
            const auto inside = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
            inside->setMargin(desktopMargin);
            inside->addChild(frame);
            desktop->addChild(inside);
            made = desktop;
        }
        return made;
    }

    void TerminalPage::showOverview() {
        reviewing = false;
        changes.clear();
        body->clearChildren();
        statusShown.clear();
        const auto environment = foundation::currentEnvironment();

        if (!desktop::findProgram("kitty", foundation::variable(environment, "PATH").value_or(""))) {
            body->addChild(paragraph("Lantern themes kitty, which is not installed. Install the kitty package, then open this page again.", palette));
            setFooter(footer, {});
            return;
        }

        // Dark or light, and which eight of them. Contrast and color vision are on the
        // Accessibility page, and named in the footer.
        const auto shown = shownThemes();
        const auto top   = leftRow();
        top->rebuild()->gap(4)->commence();
        const auto filter = [this](std::string label, double width, Kind wanted) {
            return button(std::move(label), width, kind == wanted, [this, wanted] {
                defer([this, wanted] {
                    kind      = wanted;
                    pageStart = 0;
                    showOverview();
                });
            });
        };
        top->addChild(filter("Dark", 80, Kind::dark));
        top->addChild(filter("Light", 80, Kind::light));
        const auto rest = space(1);
        rest->setGrow(true, false);
        top->addChild(rest);

        // The font and its size, applied with the theme.
        std::vector<std::string> names{"Default"};
        std::size_t              current = 0;
        for (std::size_t index = 0; index < fonts.size(); ++index) {
            names.push_back(fonts[index].installed ? fonts[index].family : std::format("{}  (not installed)", fonts[index].family));
            if (fontFamily == fonts[index].family)
                current = index + 1;
        }
        top->addChild(Hyprtoolkit::CComboboxBuilder::begin()
                          ->items(std::move(names))
                          ->currentItem(current)
                          ->onChanged([this](CSharedPointer<Hyprtoolkit::CComboboxElement>, std::size_t index) {
                              defer([this, index] {
                                  fontFamily = index == 0 ? std::nullopt : std::optional{fonts[index - 1].family};
                                  showFontHint();
                                  updateSelection();
                              });
                          })
                          ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {scaled(260.0), scaled(34.0)}})
                          ->commence());
        const auto size = [this](double step) {
            fontSize = std::clamp(fontSize + step, 6.0, 32.0);
            fontText->rebuild()->text(std::format("{:g} pt", fontSize))->commence();
            updateSelection();
        };
        fontText = markupLine(std::format("{:g} pt", fontSize), palette);
        fontText->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {54.0, 1.0}})->align(Hyprtoolkit::HT_FONT_ALIGN_CENTER)->commence();
        top->addChild(Hyprtoolkit::CNullBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {8.0, 1.0}})->commence());
        top->addChild(letterButton("A", 8.F, palette, defer, [size] { size(-1); }));
        top->addChild(fontText);
        top->addChild(letterButton("A", 14.F, palette, defer, [size] { size(1); }));
        // How see-through the background is; a value of kitty.conf's own is kept as a choice.
        std::vector<std::string> levels;
        std::size_t              level = 0;
        for (const auto& [value, name] : desktop::terminalOpacities()) {
            if (value == opacity)
                level = levels.size();
            levels.emplace_back(name);
        }
        const bool ownLevel = std::ranges::none_of(desktop::terminalOpacities(), [this](const auto& known) { return known.value == opacity; });
        if (ownLevel) {
            level = levels.size();
            levels.push_back(std::format("Yours, {:g}", opacity));
        }
        top->addChild(Hyprtoolkit::CNullBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {16.0, 1.0}})->commence());
        top->addChild(Hyprtoolkit::CComboboxBuilder::begin()
                          ->items(std::move(levels))
                          ->currentItem(level)
                          ->onChanged([this, own = opacity](CSharedPointer<Hyprtoolkit::CComboboxElement>, std::size_t index) {
                              defer([this, index, own] {
                                  const auto known = desktop::terminalOpacities();
                                  opacity          = index < known.size() ? known[index].value : own;
                                  updateSelection();
                              });
                          })
                          ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {scaled(130.0), scaled(34.0)}})
                          ->commence());
        top->addChild(Hyprtoolkit::CNullBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {24.0, 1.0}})->commence());
        body->addChild(top);
        fontHint = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        body->addChild(fontHint);
        showFontHint();
        opacityNote = markupLine(" ", palette, CFontSize{CFontSize::HT_FONT_SMALL}, 0.7F);
        body->addChild(opacityNote);

        largePreview = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        body->addChild(largePreview);

        // Two rows of cards at a time; the mouse wheel moves them a row up or down.
        grid = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(12)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        // A scroll area takes all the height the page leaves; the paged rows keep their own.
        if constexpr (scrollAreaWorks) {
            grid->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
            grid->setGrow(false, true);
        }
        body->addChild(grid);
        position = markupLine(" ", palette, CFontSize{CFontSize::HT_FONT_SMALL}, 0.6F);
        body->addChild(position);
        fillGrid();
        updateSelection();
    }

    void TerminalPage::scroll(Hyprtoolkit::Input::eAxisAxis axis, float delta) {
        if (axis != Hyprtoolkit::Input::AXIS_AXIS_VERTICAL || delta == 0.F)
            return;
        const auto count = shownThemes().size();
        const auto last  = count > perPage ? (count - perPage + perRow - 1) / perRow * perRow : 0;
        const auto next  = delta > 0 ? std::min(last, pageStart + perRow) : pageStart - std::min(pageStart, perRow);
        if (next == pageStart)
            return;
        pageStart = next;
        // Not from inside the wheel's handler: the cards it came through are replaced.
        defer([this] {
            fillGrid();
            updateSelection();
        });
    }

    TerminalPage::Card TerminalPage::makeCard(const desktop::TerminalTheme* theme) {
        Card made;
        made.frame = Hyprtoolkit::CRectangleBuilder::begin()
                         ->color([palette = palette] { return palette->m_colors.alternateBase; })
                         ->rounding(palette->m_vars.bigRounding)
                         ->borderColor([] { return Hyprtoolkit::CHyprColor{0, 0, 0, 0}; })
                         ->borderThickness(2)
                         ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {cardShare, 1.0}})
                         ->commence();
        const auto inside = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        inside->setMargin(8);
        made.holder = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        made.holder->addChild(sessionOf(theme, false));
        made.name = markupLine(escapeMarkup(theme->name), palette, CFontSize{CFontSize::HT_FONT_SMALL});
        inside->addChild(made.holder);
        inside->addChild(made.name);
        made.frame->addChild(inside);
        made.frame->setReceivesMouse(true);
        // Paged by the wheel only where the scroll area cannot be used; it scrolls by itself.
        if constexpr (!scrollAreaWorks)
            made.frame->setMouseAxis([this](Hyprtoolkit::Input::eAxisAxis axis, float delta) { scroll(axis, delta); });
        made.frame->setMouseButton([this, theme](Hyprtoolkit::Input::eMouseButton pressedButton, bool pressed) {
            if (pressedButton != Hyprtoolkit::Input::MOUSE_BUTTON_LEFT || !pressed)
                return;
            defer([this, theme] {
                selected = theme;
                updateSelection();
            });
        });
        return made;
    }

    void TerminalPage::fillGrid() {
        const auto shown = shownThemes();
        grid->clearChildren();
        cards.clear();
        // Every card in a scroll area, or the two rows paged in; see ui/toolkit.hpp.
        const auto first = scrollAreaWorks ? 0 : pageStart;
        const auto last  = scrollAreaWorks ? shown.size() : std::min(shown.size(), pageStart + perPage);
        const auto rows  = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(12)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        for (std::size_t start = first; start < last; start += perRow) {
            const auto line = leftRow(0);
            line->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, cardHeight()}})->gap(12)->commence();
            for (std::size_t index = start; index < std::min(last, start + perRow); ++index) {
                auto made = makeCard(shown[index]);
                line->addChild(made.frame);
                cards.emplace_back(shown[index], std::move(made));
            }
            rows->addChild(line);
        }
        if constexpr (scrollAreaWorks) {
            const auto area = Hyprtoolkit::CScrollAreaBuilder::begin()
                                  ->scrollY(true)
                                  ->scrollX(false)
                                  ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})
                                  ->commence();
            area->addChild(rows);
            // Opens with the chosen theme's row in view.
            if (const auto at = std::ranges::find(shown, selected); at != shown.end())
                scrollWhenLaidOut(area, Hyprutils::Math::Vector2D{0.0, static_cast<double>(static_cast<std::size_t>(at - shown.begin()) / perRow) * (cardHeight() + 12)}, defer);
            grid->addChild(area);
            position->rebuild()->text(std::format("{} themes{}", shown.size(), shown.size() > perPage ? " · scroll for more" : ""))->commence();
            return;
        }
        grid->addChild(rows);
        const auto end = std::min(shown.size(), pageStart + perPage);
        position->rebuild()
            ->text(shown.size() > perPage ? std::format("{}–{} of {} · scroll for more", pageStart + 1, end, shown.size()) : std::format("{} themes", shown.size()))
            ->commence();
    }

    void TerminalPage::updateSelection() {
        largePreview->clearChildren();
        largePreview->addChild(sessionOf(selected, true));
        opacityNote->rebuild()
            ->text(contrast == desktop::Contrast::high && opacity < 1 ? escapeMarkup("High contrast is on, and a see-through background lowers it: Solid reads best.")
                                                                       : std::string{" "})
            ->commence();
        fillStatus();
        for (auto& [theme, made] : cards) {
            const bool chosen = theme == selected;
            made.frame->rebuild()->borderColor([palette = palette, chosen] { return chosen ? palette->m_colors.accent : Hyprtoolkit::CHyprColor{0, 0, 0, 0}; })->commence();
            auto name = escapeMarkup(theme->name);
            if (chosen)
                name = std::format("<span weight=\"600\">{}</span>", name);
            if (theme == inUse)
                name += std::format("  {}", colored("· in use", hex(palette->m_colors.accent)));
            made.name->rebuild()->text(std::string{name})->commence();
        }
    }

    void TerminalPage::fillStatus() {
        const auto environment = foundation::currentEnvironment();
        const auto planned     = desktop::planTerminal(*selected, fontSize, usableFamily(), opacity, hyprlandConfig(), environment);
        const bool undoable    = configuration::latestChange(application::backupsFolder(environment), changeName).has_value();
        inUse                  = desktop::currentTerminal(environment);
        const auto shown = std::format("{}|{}|{}|{}|{}|{}|{}|{}", fullName(*selected), fontSize, usableFamily().value_or(""), opacity, planned ? planned->empty() : false,
                                       planned ? "" : planned.error(), undoable, outcome);
        if (shown == statusShown)
            return;
        statusShown = shown;

        // Said in the footer, so that saying it moves nothing above.
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> buttons;
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> away;
        if (undoable)
            away.push_back(button("Undo last change", 160, false, [this] { defer([this] { undo(); }); }));
        std::string note;
        if (!planned) {
            note = escapeMarkup(std::format("Lantern cannot apply it: {}.", planned.error()));
        } else if (planned->empty()) {
            note = std::format("{}  Your terminal uses {}, in {} at {:g} pt.", colored("✓", hex(palette->m_colors.accent)), escapeMarkup(fullName(*selected)),
                               escapeMarkup(desktop::kittyFontFamily(environment).value_or("kitty's default font")), fontSize);
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

    void TerminalPage::showReview() {
        const auto environment = foundation::currentEnvironment();
        auto       planned     = desktop::planTerminal(*selected, fontSize, usableFamily(), opacity, hyprlandConfig(), environment);
        if (!planned) {
            outcome = planned.error();
            return showOverview();
        }
        reviewing = true;
        changes   = std::move(*planned);
        outcome.clear();
        body->clearChildren();
        body->addChild(space(4));
        body->addChild(heading(std::format("Review: {}, in {} at {:g} pt, for the terminal", fullName(*selected), usableFamily().value_or("kitty's default font"), fontSize),
                               palette));
        body->addChild(paragraph("Applying will:", palette));
        body->addChild(paragraph("•  Write the changes below. A copy of each file is kept first, so it can be undone.", palette, true));
        body->addChild(paragraph("•  Show the new colors and font size in every open kitty window at once.", palette, true));
        // kitty takes a new opacity on reload only if it started with dynamic opacity on.
        if (opacity != desktop::kittyOpacity(environment) && !desktop::kittyDynamicOpacity(environment))
            body->addChild(paragraph("•  Give new kitty windows the new background. Windows open now keep theirs until they are opened again; after that, "
                                     "changes reach them at once.",
                                     palette, true));
        setFooter(footer, {button("Cancel", 110, false, [this] { defer([this] { showOverview(); }); }), button("Apply", 110, true, [this] { defer([this] { apply(); }); })});
        diff.setRows(diffRows(changes, *palette));
        body->addChild(diff.element());
    }

    void TerminalPage::apply() {
        if (!reviewing)
            return;
        const auto record =
            configuration::applyChanges(changes, application::backupsFolder(foundation::currentEnvironment()), std::format("{}: {}", changeName, fullName(*selected)));
        if (!record) {
            outcome = std::format("Nothing was written: {}.", record.error());
            return showOverview();
        }
        // kitty reads kitty.conf, and the theme it includes, again on SIGUSR1.
        desktop::signalProcesses("kitty", SIGUSR1);
        outcome = std::format("Applied {}.", fullName(*selected));
        const auto environment = foundation::currentEnvironment();
        const auto hyprland    = hyprlandConfig();
        const auto hyprctl     = desktop::findProgram("hyprctl", foundation::variable(environment, "PATH").value_or(""));
        if (hyprland && hyprctl && std::ranges::any_of(changes, [&hyprland](const auto& change) { return change.file == hyprland->file; }))
            application::checkHyprlandConfig(loop, *hyprctl, *record, [this](std::optional<std::string> problem) {
                if (!problem)
                    return;
                outcome = *problem;
                if (!reviewing)
                    showOverview();
            });
        showOverview();
    }

    void TerminalPage::undo() {
        const auto latest = configuration::latestChange(application::backupsFolder(foundation::currentEnvironment()), changeName);
        if (!latest)
            return showOverview();
        if (const auto undone = configuration::undoChange(*latest); !undone) {
            outcome = std::format("Nothing was undone: {}.", undone.error());
        } else {
            desktop::signalProcesses("kitty", SIGUSR1);
            outcome = "Undone.";
        }
        showOverview();
    }

    void TerminalPage::embed() {
        root->removeChild(title);
        root->setMargin(0);
    }

}
