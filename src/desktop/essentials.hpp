// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "desktop/waybar.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace desktop {

    // Something a Hyprland desktop needs beside Hyprland, and whether this system has it.
    struct Essential {
        Requirement                          requirement;
        // What is wrong with it although it was found, such as a program that is not running.
        std::string                          problem;
        // A program that is installed but works only while it runs, and is not running:
        // Lantern can start it, now and when Hyprland starts.
        std::optional<std::filesystem::path> start;
    };

    // Where the checks look, so that a test can point them at a folder of its own.
    struct EssentialSources {
        std::filesystem::path                   root      = "/";
        std::filesystem::path                   processes = "/proc";
        std::function<bool(const std::string&)> hasFont   = hasFontFamily;
    };

    // The screen-sharing and file-dialog portals, a polkit agent for password prompts, a
    // notification daemon, fonts for text and emoji, and Qt's Wayland support.
    [[nodiscard]] std::vector<Essential> essentials(const EssentialSources& sources = {});

}
