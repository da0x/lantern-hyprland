// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "configuration/recording.hpp"
#include "ipc/plugins.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace application {

    // A Hyprland plugin: one that is loaded, one the config loads, or one Lantern turned off.
    struct Plugin {
        // Hyprland's name for it, or the file's name while it is not loaded.
        std::string                          name;
        std::string                          author;
        std::string                          version;
        std::string                          description;
        // The file it is loaded from. Missing for a loaded plugin whose file Lantern could
        // not find, which then cannot be turned off from Lantern.
        std::optional<std::filesystem::path> file;
        bool                                 loaded     = false;
        // The config loads it, so it comes back when Hyprland starts.
        bool                                 inConfig   = false;
        // Lantern turned it off and remembers it, so it can be turned on again.
        bool                                 remembered = false;

        friend bool operator==(const Plugin&, const Plugin&) = default;
    };

    // Every plugin to show, loaded ones first in Hyprland's order, then the ones the config
    // loads, then the ones Lantern turned off. A loaded plugin's file is the library
    // Hyprland mapped whose name matches it, as hypr3d matches hypr3d.so and libhypr3d.so;
    // a file also in `configured` or `remembered` is taken first. Remembered files that are
    // gone are left out.
    [[nodiscard]] std::vector<Plugin> listPlugins(const std::vector<ipc::LoadedPlugin>& loaded, const std::vector<std::filesystem::path>& mapped,
                                                  const std::vector<std::filesystem::path>& configured, const std::vector<std::filesystem::path>& remembered);

    // The files a Lua config loads with hl.plugin.load("path"), in its order, with ~/
    // expanded. A path the config computes is still read, since the recording holds the value.
    [[nodiscard]] std::vector<std::filesystem::path> pluginsInConfig(const configuration::Recording& recording, const std::filesystem::path& home);

    // The same for hyprland.conf: its plugin = path lines, not those commented out.
    [[nodiscard]] std::vector<std::filesystem::path> pluginsInConfig(std::string_view hyprlangConfig, const std::filesystem::path& home);

}
