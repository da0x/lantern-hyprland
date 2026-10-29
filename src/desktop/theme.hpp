// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "configuration/file-change.hpp"
#include "configuration/main-config.hpp"
#include "desktop/gsettings.hpp"
#include "desktop/waybar-style.hpp"
#include "foundation/environment.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace desktop {

    // One palette for the whole desktop: the windows' borders, Waybar, and hyprtoolkit's
    // apps, Lantern among them.

    // The border values hl.config gets, as Lua: a gradient of the accent and the audio
    // color for the focused window, the raised color for the others.
    [[nodiscard]] std::string activeBorder(const Palette& palette);
    [[nodiscard]] std::string inactiveBorder(const Palette& palette);

    // hyprtoolkit.conf's color keys for the palette.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> hyprtoolkitColors(const Palette& palette);

    // $XDG_CONFIG_HOME/hypr/hyprtoolkit.conf, or ~/.config/hypr/hyprtoolkit.conf.
    [[nodiscard]] std::filesystem::path hyprtoolkitConfigFile(const foundation::Environment& environment);

    // Every file change that gives the desktop `palette`: the Hyprland config (only a Lua
    // config, through its hl.config borders; a linked config is followed), Waybar as
    // `bar` is, in the new palette and the windows' geometry, hyprtoolkit.conf, and, for those installed, wofi's style.css and dunst's
    // dunstrc (started from the system's when there is none). Only files that change are
    // listed. The terminal has themes of its own; see terminal-theme.hpp.
    [[nodiscard]] std::expected<std::vector<configuration::FileChange>, std::string> planTheme(const Palette& palette, WaybarStyle bar, const configuration::MainConfig& config,
                                                                                               const foundation::Environment& environment);

    // The GTK settings that differ from what `palette` wants: color-scheme, dark or light,
    // and accent-color. Empty when gsettings is not installed.
    [[nodiscard]] std::vector<GSetting> gtkChanges(const Palette& palette, const foundation::Environment& environment);

    // wofi's stylesheet for the palette.
    [[nodiscard]] std::string wofiStyle(const Palette& palette);

    // A stylesheet that gives GTK apps the palette, for GTK 3 and GTK 4 alike. Adwaita
    // builds its colors into its rules, so renaming them is not enough: the rules here
    // cover what the volume control, the network settings and most small apps are made of.
    [[nodiscard]] std::string gtkStyle(const Palette& palette);

    // The palette whose borders the Hyprland config sets now, if it is one of Lantern's.
    [[nodiscard]] const Palette* currentTheme(const configuration::MainConfig& config);

}
