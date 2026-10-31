// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace foundation {

    // Environment variables by name.
    using Environment = std::map<std::string, std::string>;

    // Lantern's own environment.
    [[nodiscard]] Environment currentEnvironment();

    [[nodiscard]] std::optional<std::string> variable(const Environment& environment, const std::string& name);

    // Everything Lantern keeps for itself: $XDG_CONFIG_HOME/lantern-hyprland, or
    // ~/.config/lantern-hyprland.
    [[nodiscard]] std::filesystem::path lanternFolder(const Environment& environment);

}
