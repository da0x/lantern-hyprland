// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "configuration/file-change.hpp"
#include "desktop/essentials.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    // What a Hyprland desktop needs beside Hyprland, such as the portal for screen sharing
    // and an agent for password prompts, whether each is there, and the command that
    // installs one that is not. Lantern installs nothing itself; it only starts a program
    // that is installed but not running, and adds it to Hyprland's start, after the user
    // reviews the change.
    class EssentialsPage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        EssentialsPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Checks again, when the page is opened.
        void refresh();

        // Draws the page again, after Lantern's colors or text size changed.
        void restyle();

      private:
        void showOverview();
        void showReview();
        void apply();
        void undo();
        void fillStatus();
        // The essential Lantern can start, if one is not running.
        [[nodiscard]] const desktop::Essential* startable() const;
        [[nodiscard]] std::expected<std::optional<configuration::FileChange>, std::string> plan() const;

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        application::EventLoop                                               loop;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> body;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    footer;
        RowList                                                              diff;

        std::vector<desktop::Essential>                                      found;
        bool                                                                 reviewing = false;
        // The change that starts it with Hyprland; none when the config already does.
        std::optional<configuration::FileChange>                             change;
        std::string                                                          outcome;
    };

}
