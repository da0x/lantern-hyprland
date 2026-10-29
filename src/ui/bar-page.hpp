// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "application/settings.hpp"
#include "configuration/file-change.hpp"
#include "desktop/waybar-style.hpp"
#include "ui/program-switch.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/palette/Palette.hpp>
#include <hyprtoolkit/window/Window.hpp>
#include <hyprutils/signal/Signal.hpp>

#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <tuple>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ui {

    // The bar at the top of the screen. Lantern ships no bar of its own: it checks what
    // Waybar needs and shows the command that installs anything missing, turns Waybar on
    // and off, and offers styles for it, written only after the user reviews the change.
    class BarPage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        BarPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Looks again, as after the user installed something.
        void check();

        // Stops watching Waybar. Called before the event loop goes away.
        void close();

        // The window the menu that adds a widget opens from.
        void setWindow(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IWindow> window);

        // Draws the page again, after Lantern's colors changed.
        void restyle();

      private:
        void showOverview();
        void showReview(const desktop::WaybarStyle& style);
        // Waybar's files for `style`, and the rules in the Hyprland config that float what
        // the bar opens, each only where it changes something.
        [[nodiscard]] static std::expected<std::vector<configuration::FileChange>, std::string> plan(const desktop::WaybarStyle& style);
        void apply();
        void undo();
        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> styleSection();
        // A choice drawn as a preview with its name; the chosen one has the accent around it.
        struct Card {
            Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRectangleElement>    frame;
            Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> holder;
            Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         name;
            std::string                                                          title;
        };
        // `nameFirst` puts the name above what the card holds.
        [[nodiscard]] Card card(const std::string& name, std::function<void()> chosenNow, bool nameFirst = false);
        // After a click on a card: changes the previews, borders and status in place, so
        // nothing else is made anew. hyprtoolkit draws a new text a frame or two after it is
        // made, so rebuilding the page on every click made it flicker.
        void                                                                   updateStyle();
        void                                                                   fillStyleStatus();
        // The open tab's choices: the shape, colors and border, or the widgets.
        void                                                                   fillChoices();
        // The Pop-ups tab, its status, and its own review, apart from the bar's style.
        void                                                                   fillPopups();
        void                                                                   fillPopupStatus();
        void                                                                   showPopupReview();
        void                                                                   applyPopups();
        void                                                                   undoPopups();
        // Starts swayosd's server again, so it reads its style, when the pop-ups are on.
        void                                                                   restartPopups();
        [[nodiscard]] std::expected<std::vector<configuration::FileChange>, std::string> planPopups() const;
        // A menu, at the pointer, of the widgets to add to `side`.
        void                                                                   openAddMenu(desktop::Side side, std::vector<desktop::Widget> off);
        // A drawing of a style, made once and kept: `kind` 0 is the large one, 1 a shape
        // card's, 2 a palette card's, 3 a border card's. An element has one parent, so each place has its own.
        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> previewOf(int kind, const desktop::WaybarStyle& style);

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        application::EventLoop                                               loop;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> body;
        // The page's action buttons, at the bottom right.
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    footer;
        RowList                                                              diff;

        ProgramSwitch                                                        waybar;

        application::Settings                                                settings;
        // The style shown in the big preview: Waybar's own at first, if it is one of Lantern's.
        desktop::WaybarStyle                                                 selected;
        std::map<std::tuple<int, desktop::Shape, const desktop::Palette*, desktop::Border, desktop::WidgetSides>, Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement>> previews;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> largePreview;
        // What the status shows now, so that it is made anew only when that changes.
        std::string                                                          styleStatusShown;
        std::vector<std::pair<desktop::Shape, Card>>                         shapeCards;
        std::vector<std::pair<const desktop::Palette*, Card>>                paletteCards;
        std::vector<std::pair<desktop::Border, Card>>                        borderCards;
        // The Style, Widgets and Pop-ups tabs, and what the one open shows.
        enum class Tab { style, widgets, popups };
        std::vector<Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement>> tabButtons;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> choices;
        Tab                                                                  tab = Tab::style;
        // The volume and brightness pop-ups: whether the config starts them now, whether
        // the user wants them, and whether their change is under review.
        bool                                                                 popupsNow       = false;
        bool                                                                 popupsWanted    = false;
        bool                                                                 numpadNow       = false;
        bool                                                                 numpadWanted    = false;
        bool                                                                 reviewingPopups = false;
        // Lantern's window, which the menu that adds a widget opens from, and that menu.
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IWindow>              window;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IWindow>              menu;
        Hyprutils::Signal::CHyprSignalListener                               menuClosed;
        // The style under review and the files it would write.
        std::optional<desktop::WaybarStyle>                                  reviewing;
        std::vector<configuration::FileChange>                               changes;
        // What happened last, shown under the style.
        std::string                                                          outcome;
    };

}
