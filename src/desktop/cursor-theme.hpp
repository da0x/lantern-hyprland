// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "configuration/file-change.hpp"
#include "configuration/main-config.hpp"
#include "desktop/gsettings.hpp"
#include "foundation/environment.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace desktop {

    // A cursor theme: one installed, or one Lantern recommends from the distribution's
    // packages.
    struct CursorTheme {
        // Its folder's name, which XCURSOR_THEME and hyprctl setcursor take.
        std::string           name;
        // What it calls itself, in its index.theme, or the folder's name.
        std::string           title;
        // Where it is installed; empty when it is not.
        std::filesystem::path folder;
        // The Arch Linux package it comes in, for one Lantern recommends.
        std::string           package;
    };

    // The cursor themes installed, by title, then those Lantern recommends that are not.
    [[nodiscard]] std::vector<CursorTheme> cursorThemes(const foundation::Environment& environment);

    // The theme Hyprland's cursor comes from: the config's XCURSOR_THEME, or GTK's
    // cursor-theme, with "default" followed to the theme it inherits.
    [[nodiscard]] std::string currentCursorTheme(const configuration::MainConfig& config, const foundation::Environment& environment);

    // One of a theme's cursors as a PNG, at the size nearest `size`: the first of `names`
    // that the theme has, since themes name the same cursor differently.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> cursorImage(const std::filesystem::path& folder, const std::vector<std::string_view>& names, int size);

    // What gives the desktop `theme`: XCURSOR_THEME in the Hyprland config (only a Lua
    // config; a linked config is followed), and ~/.icons/default/index.theme, which X11
    // apps read. Only files that change are listed.
    [[nodiscard]] std::expected<std::vector<configuration::FileChange>, std::string> planCursorTheme(const CursorTheme& theme, const configuration::MainConfig& config,
                                                                                                    const foundation::Environment& environment);

    // GTK's cursor-theme, if it differs from `theme`.
    [[nodiscard]] std::vector<GSetting> cursorGsettings(const CursorTheme& theme, const foundation::Environment& environment);

}
