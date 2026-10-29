// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "configuration/file-change.hpp"
#include "desktop/night-light.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace ui {

    // The night light: how warm the screens turn, and when, through hyprsunset, with the
    // warmth tried on the screens at once. Written only after the user reviews the change.
    class NightLightPage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        NightLightPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Without its title and margin, as a tab of a page that has them; see TabbedPage.
        void embed();

        // Reads the files again, when the page is opened.
        void refresh();

        // Draws the page again, after Lantern's colors or text size changed.
        void restyle();

        // Puts the screens back as hyprsunset's profile has them, after a warmth was tried.
        void leave();

      private:
        void showOverview();
        void showReview();
        void apply();
        void undo();
        void fillStatus();
        // Starts hyprsunset again, so that it reads its new config, or stops it.
        void restart();
        // Shows a warmth on the screens now, or what hyprsunset's profile has.
        void tryWarmth(std::optional<int> temperature);
        [[nodiscard]] std::expected<std::vector<configuration::FileChange>, std::string> plan() const;

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        application::EventLoop                                               loop;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         title;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> body;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    footer;
        RowList                                                              diff;

        // On means hyprsunset starts with Hyprland and its config has the evening and morning.
        bool                                                                 onNow    = false;
        bool                                                                 onWanted = false;
        desktop::NightLight                                                  now;
        desktop::NightLight                                                  wanted;
        bool                                                                 trying    = false;
        bool                                                                 reviewing = false;
        std::vector<configuration::FileChange>                               changes;
        std::string                                                          outcome;
    };

}
