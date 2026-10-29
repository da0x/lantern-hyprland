// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace application {

    // Lantern's own settings, kept in settings.json in its folder.
    struct Settings {
        // The user has seen that everything the bar needs is installed, and hid the list.
        bool barRequirementsDismissed       = false;
        // The same for the Wallpaper page.
        bool        wallpaperRequirementsDismissed = false;
        // The wallpaper program the Wallpaper page uses: "hyprpaper" or "awww", or empty
        // before the user chose.
        std::string wallpaperProgram;
        // The fit of the wallpaper last kept with awww, which awww does not report back:
        // "cover", "contain" or "fill".
        std::string awwwFit = "cover";
        // The contrast and color vision chosen on the Accessibility page, which every
        // terminal theme follows: "standard" or "high", and "typical", "red-green" or
        // "blue-yellow". Empty before the user chose.
        std::string contrast;
        std::string vision;
        // The areas the user allowed on the Activate page, by name: "options",
        // "keybindings", "programs", "theme". None until the user allows one.
        std::vector<std::string> activationAllowed;
    };

    // Missing or unreadable settings are the defaults.
    [[nodiscard]] Settings loadSettings(const std::filesystem::path& lanternFolder);

    // Keeps any setting in the file that this version does not know.
    [[nodiscard]] std::expected<void, std::string> saveSettings(const Settings& settings, const std::filesystem::path& lanternFolder);

}
