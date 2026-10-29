// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "configuration/file-change.hpp"
#include "desktop/terminal-theme.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <tuple>
#include <string>
#include <vector>

namespace ui {

    // The terminal's colors, chosen apart from the desktop's from terminal themes of their
    // own, and written to kitty only after the user reviews the change.
    class TerminalPage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        TerminalPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Without its title and margin, as a tab of a page that has them; see TabbedPage.
        void embed();

        // Draws the page again, after Lantern's colors changed.
        void restyle();

        // Shows every theme adjusted for this contrast and color vision, set on the
        // Accessibility page, keeping the same theme chosen.
        void adjust(desktop::Contrast wanted, desktop::Vision vision);

      private:
        void showOverview();
        void showReview();
        void apply();
        void undo();
        void updateSelection();
        // Makes the cards for the rows shown.
        void fillGrid();
        void scroll(Hyprtoolkit::Input::eAxisAxis axis, float delta);
        void fillStatus();
        // Every theme, adjusted for the contrast and color vision chosen.
        [[nodiscard]] std::span<const desktop::TerminalTheme>                  themes() const;
        // The themes shown: the dark ones or the light ones.
        [[nodiscard]] std::vector<const desktop::TerminalTheme*>               shownThemes() const;
        // A short terminal session in a theme, made once and kept: large or for a card.
        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> sessionOf(const desktop::TerminalTheme* theme, bool large);

        struct Card {
            Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRectangleElement>    frame;
            Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> holder;
            Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         name;
        };
        [[nodiscard]] Card makeCard(const desktop::TerminalTheme* theme);

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        application::EventLoop                                               loop;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         title;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> body;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    footer;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> largePreview;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> grid;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         position;
        std::vector<std::pair<const desktop::TerminalTheme*, Card>>          cards;
        std::map<std::tuple<const desktop::TerminalTheme*, bool, double, std::string, double>, Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement>> sessions;
        // Under the filters: how to install the font chosen, when it is not installed.
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> fontHint;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         fontText;
        // Under the filters: that a see-through background lowers the contrast, when high
        // contrast is on.
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         opacityNote;
        RowList                                                              diff;

        const desktop::TerminalTheme*                                        selected = nullptr;
        const desktop::TerminalTheme*                                        inUse    = nullptr;
        enum class Kind {
            dark,
            light,
        };
        Kind                                                                 kind     = Kind::dark;
        desktop::Contrast                                                    contrast = desktop::Contrast::standard;
        desktop::Vision                                                      vision   = desktop::Vision::typical;
        // The font size to give kitty, in points; kitty's own until changed.
        double                                                               fontSize = 11;
        // The font family to give kitty, or nothing for kitty's own; and the fonts to choose from.
        std::optional<std::string>                                           fontFamily;
        std::vector<desktop::TerminalFont>                                   fonts;
        // How much of the desktop shows through kitty's background; kitty's own until changed.
        double                                                               opacity  = 1;
        // The desktop's image, drawn behind the large preview so its opacity shows.
        std::optional<std::filesystem::path>                                 wallpaperImage;
        // The font family the plan and the preview use: the one chosen, if it is installed.
        [[nodiscard]] std::optional<std::string>                             usableFamily() const;
        void                                                                 showFontHint();
        std::size_t                                                          pageStart  = 0;
        bool                                                                 reviewing  = false;
        std::vector<configuration::FileChange>                               changes;
        std::string                                                          outcome;
        std::string                                                          statusShown;
    };

}
