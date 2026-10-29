// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "configuration/file-change.hpp"
#include "desktop/displays.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    // Each display's resolution, refresh rate, scale, rotation, variable refresh rate and
    // place beside the others. Written to the Hyprland config only after the user reviews
    // the change, and put back after a few seconds unless the user keeps it, since a
    // display that shows nothing cannot be used to undo it.
    class DisplaysPage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        DisplaysPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Without its title and margin, as a tab of a page that has them; see TabbedPage.
        void embed();

        // Asks Hyprland for its displays again, when the page is opened.
        void refresh();

        // Draws the page again, after Lantern's colors or text size changed.
        void restyle();

      private:
        void showOverview();
        void showReview();
        void apply();
        void undo();
        // Puts the change being tried back, or keeps it.
        void putBack(const std::string& why);
        void keep();
        void countDown(unsigned trial);
        [[nodiscard]] std::expected<std::optional<configuration::FileChange>, std::string> plan() const;
        void fillMap();
        void fillSettings();
        void fillSharpApps();
        void fillStatus();

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        application::EventLoop                                               loop;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         title;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> body;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    footer;
        RowList                                                              diff;

        // What Hyprland has now, and what is chosen for each, in the same order.
        std::vector<desktop::Display>                                        displays;
        std::vector<desktop::DisplaySettings>                                wanted;
        std::size_t                                                          selected = 0;
        // Whether X11 apps are kept sharp: as the config has it, and as chosen.
        std::optional<desktop::SharpApps>                                    sharpNow;
        bool                                                                 sharpWanted = false;
        // Why the displays could not be read, if they could not.
        std::string                                                          problem;
        bool                                                                 reviewing = false;
        configuration::FileChange                                            change;
        // The change being tried, until it is kept or put back, and the seconds left.
        std::optional<std::filesystem::path>                                 trying;
        int                                                                  secondsLeft = 0;
        unsigned                                                             trial       = 0;
        std::string                                                          outcome;
    };

}
