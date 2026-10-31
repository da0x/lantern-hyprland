// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ipc/instance.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ipc {

    // A plugin the running Hyprland has loaded, as it describes itself.
    struct LoadedPlugin {
        std::string name;
        std::string author;
        std::string version;
        std::string description;

        friend bool operator==(const LoadedPlugin&, const LoadedPlugin&) = default;
    };

    // Reads the reply to `hyprctl -j plugin list`, in Hyprland's order.
    [[nodiscard]] std::expected<std::vector<LoadedPlugin>, std::string> parseLoadedPlugins(std::string_view reply);

    // Every shared library the running Hyprland has mapped, plugins among them, each once.
    // Hyprland's plugin list leaves out the file a plugin came from, which turning it off
    // and on again needs, so it is read from the process instead: its ID from the lock file
    // in the instance's folder, its libraries from `processRoot`/ID/maps. Empty when either
    // cannot be read. See docs/upstream.md.
    [[nodiscard]] std::vector<std::filesystem::path> mappedLibraries(const Instance& instance, const std::filesystem::path& processRoot = "/proc");

}
