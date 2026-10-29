// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/environment.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace configuration {

    // What Hyprland was started with, which is everything its choice of config depends on.
    struct Launch {
        // The command line without the program's name.
        std::vector<std::string>           arguments;
        foundation::Environment            environment;
        std::filesystem::path              workingFolder;
    };

    // How Hyprland came to read the config.
    enum class Choice {
        // --config FILE, or -c FILE.
        commandLine,
        // HYPRLAND_CONFIG.
        environment,
        // The first hyprland.lua, or failing that hyprland.conf, in the config folders.
        search,
    };

    struct MainConfig {
        std::filesystem::path file;
        Choice                choice = Choice::search;

        // Started with --safe-mode, as after a crash. Hyprland then runs a recovery config
        // of its own until the user loads this one from its dialog, which nothing reports.
        bool                  safeMode = false;

        [[nodiscard]] bool    lua() const { return file.extension() == ".lua"; }
    };

    // The config Hyprland chose when it started with `launch`, by Hyprland's own rule:
    // --config, then HYPRLAND_CONFIG, then the first hyprland.lua and then the first
    // hyprland.conf in $XDG_CONFIG_HOME, ~/.config, each of $XDG_CONFIG_DIRS and
    // `systemFolder`. Hyprland chooses once, at startup, while this looks at the files as
    // they are now.
    [[nodiscard]] std::expected<MainConfig, std::string> mainConfig(const Launch& launch, const std::filesystem::path& systemFolder = "/etc/xdg");

    // Lantern's own command line and environment, for when Hyprland's cannot be read.
    // Lantern is usually started from Hyprland and inherits its environment.
    [[nodiscard]] Launch lanternLaunch();

}
