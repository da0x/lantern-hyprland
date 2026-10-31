// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/plugins.hpp"
#include "application/session.hpp"

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    // Hyprland's plugins: the loaded ones, the ones the config loads, and the ones Lantern
    // turned off, with On and Off for each. Like an outside program's switch, pressing one
    // is the request, it takes effect at once, and it never changes what loads at login.
    class PluginsPage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        PluginsPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Asks Hyprland again, when the page is opened and after On or Off. Hyprland sends
        // no event when a plugin is loaded or unloaded; see docs/upstream.md.
        void refresh();

        // Draws the page again, after Lantern's colors or text size changed.
        void restyle();

      private:
        void show();
        void addPlugin(const application::Plugin& plugin);
        // Loads the plugin's file, or unloads it, then remembers or forgets it.
        void turn(const application::Plugin& plugin, bool on);
        void forget(const std::filesystem::path& file);

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        application::EventLoop                                               loop;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> body;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    footer;

        std::vector<application::Plugin>                                     plugins;
        // Why there is no list, when there is not.
        std::string                                                          problem;
        bool                                                                 asked = false;
        // The file being loaded or unloaded, until hyprctl answers.
        std::optional<std::filesystem::path>                                 turning;
        std::string                                                          outcome;
    };

}
