// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "configuration/file-change.hpp"
#include "desktop/theme.hpp"
#include "desktop/user-themes.hpp"
#include "desktop/waybar-style.hpp"
#include "ui/program-switch.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    // One palette for the whole desktop: the windows' borders, Waybar, and the apps built on
    // hyprtoolkit, Lantern among them. Written only after the user reviews the change.
    class ThemePage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        ThemePage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Stops watching Waybar. Called before the event loop goes away.
        void close();

        // Draws the page again, after Lantern's text size changed.
        void restyle();

        // What to redraw once Lantern's palette has new colors.
        void onRecolored(std::function<void()> recolored) { this->recolored = std::move(recolored); }

      private:
        void showOverview();
        // The editor of a theme of the user's own: a new one started from `from`, or the
        // one in `file`.
        void showEditor(const desktop::Palette& from, std::optional<std::filesystem::path> file);
        // After a color or the name changed: the preview, the swatches and the footer.
        void updateEditor();
        void saveDraft();
        void deleteDraft();
        void showReview();
        void apply();
        void undo();
        // After a click on a palette: changes the preview, the borders and the footer in place.
        void updateSelection();
        // Asks dunst to read its files again.
        void reloadApps();
        void fillStatus();
        // A drawing of the desktop in a palette, made once and kept: large or for a card.
        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> desktopOf(const desktop::Palette* shown, bool large);

        struct Card {
            Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRectangleElement>    frame;
            Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> holder;
            Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         name;
        };

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        application::EventLoop                                               loop;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> body;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    footer;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> largePreview;
        // The rows of cards under the large preview, measured to size it.
        std::vector<Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>> cardRows;
        // The large preview's height: what the cards leave of the page, up to its full
        // height, or none when too little is left.
        double                                                               previewHeight = 250;
        // After a layout pass: sizes the large preview to the height left.
        void                                                                 fitPreview();
        std::string                                                          statusShown;
        std::vector<std::pair<const desktop::Palette*, Card>>                cards;
        std::map<std::pair<const desktop::Palette*, bool>, Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement>> desktops;
        RowList                                                              diff;
        // Waybar, to restart it after its files change. Its switch is on the Bar page.
        ProgramSwitch                                                        waybar;

        const desktop::Palette*                                              selected = nullptr;
        // The palette the desktop uses now, marked on its card.
        const desktop::Palette*                                              inUse    = nullptr;
        bool                                                                 reviewing = false;
        std::vector<configuration::FileChange>                               changes;
        std::vector<desktop::GSetting>                                       gtk;
        std::string                                                          outcome;
        std::function<void()>                                                recolored;

        // The theme being edited, the file it is kept in once saved, and what shows it.
        bool                                                                 editing = false;
        desktop::ThemeDraft                                                  draft;
        std::string                                                          draftAccent;
        std::optional<std::filesystem::path>                                 draftFile;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> draftPreview;
        std::vector<Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRectangleElement>> swatches;
    };

}
