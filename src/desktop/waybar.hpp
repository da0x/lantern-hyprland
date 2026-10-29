// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "configuration/file-change.hpp"
#include "configuration/main-config.hpp"
#include "desktop/system.hpp"
#include "foundation/environment.hpp"

#include <filesystem>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace desktop {

    // Something the bar needs that Lantern does not install, with how to get it.
    struct Requirement {
        std::string                name;
        // Why the bar needs it.
        std::string                purpose;
        std::string                upstream;
        // Its package on Arch Linux.
        std::string                package;
        // Where it was found, or nothing when it is missing.
        std::optional<std::string> found;

        [[nodiscard]] bool         met() const { return found.has_value(); }
    };

    // The command that installs it, for the user to run. Empty on a distribution whose
    // packages Lantern does not know.
    [[nodiscard]] std::string installCommand(const Requirement& requirement, const Distribution& distribution);

    // Waybar itself, and the font its icons come from.
    [[nodiscard]] std::vector<Requirement> waybarRequirements(const foundation::Environment& environment);

    // The config Waybar reads when it is started without --config, by Waybar's own rule:
    // config, then config.jsonc, in $WAYBAR_CONFIG_DIR, then in each of
    // $XDG_CONFIG_HOME/waybar, ~/.config/waybar, ~/waybar and `systemFolder`/waybar.
    // A window the bar opens when one of its parts is clicked, such as the volume control.
    struct Popup {
        // The rule's name in the Hyprland config, which is how Lantern finds it again.
        std::string_view rule;
        std::string_view program;
        // The class its window has, as a regular expression for the rule.
        std::string_view windowClass;
    };

    // What the volume and the network open when clicked.
    [[nodiscard]] std::span<const Popup> waybarPopups();

    // A Lua config with a window rule for each popup that floats it under the right end of
    // the bar, near the parts that open it. Rules already there, by name, are left as they are.
    [[nodiscard]] std::string withPopupRules(std::string_view config);

    // The change that adds the popups' rules to the Hyprland config (only a Lua one; a
    // linked config is followed), or nothing when they are all there.
    [[nodiscard]] std::expected<std::optional<configuration::FileChange>, std::string> planPopupRules(const configuration::MainConfig& config);

    [[nodiscard]] std::optional<std::filesystem::path> waybarConfig(const foundation::Environment& environment, const std::filesystem::path& systemFolder = "/etc/xdg");

}
