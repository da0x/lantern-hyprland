// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "configuration/file-change.hpp"
#include "configuration/main-config.hpp"

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace desktop {

    // A resolution and how often it is drawn, in hertz.
    struct Mode {
        int    width   = 0;
        int    height  = 0;
        double refresh = 0;

        friend bool operator==(const Mode&, const Mode&) = default;
    };

    // How a display is set: what Hyprland's monitor rule says for it.
    struct DisplaySettings {
        Mode   mode;
        // Its top left corner, in the layout's pixels.
        int    x = 0, y = 0;
        double scale = 1;
        // 0 upright, then 1, 2 and 3 for 90, 180 and 270 degrees; 4 to 7 the same, flipped.
        int    transform = 0;
        // Variable refresh rate: 0 off, 1 on, 2 for full-screen windows only.
        int    vrr     = 0;
        bool   enabled = true;

        friend bool operator==(const DisplaySettings&, const DisplaySettings&) = default;
    };

    // A display Hyprland knows, connected, turned on or not.
    struct Display {
        // The connector, such as DP-1, which may change from one dock or boot to the next.
        std::string       name;
        // Its maker, model and serial number, which do not.
        std::string       description;
        // What it can show, each once, in Hyprland's order: largest and fastest first.
        std::vector<Mode> modes;
        DisplaySettings   settings;

        friend bool operator==(const Display&, const Display&) = default;
    };

    // Reads the reply to `hyprctl -j monitors all`.
    [[nodiscard]] std::expected<std::vector<Display>, std::string> parseDisplays(std::string_view reply);

    // As a monitor rule writes it: 1920x1080@60, or @59.95.
    [[nodiscard]] std::string modeText(const Mode& mode);

    // Its size in the layout: its pixels divided by its scale, on their side when turned.
    [[nodiscard]] std::pair<int, int> layoutSize(const DisplaySettings& settings);

    // The scales Hyprland takes for `mode`, those that divide it into whole pixels, and
    // `current` too.
    [[nodiscard]] std::vector<double> scalesFor(const Mode& mode, double current);

    enum class Beside {
        left,
        right,
        above,
        below,
    };

    // Where a display set as `placed` goes beside one set as `other`, their tops or left
    // edges lined up.
    [[nodiscard]] std::pair<int, int> positionBeside(const DisplaySettings& placed, const DisplaySettings& other, Beside side);

    // Whether apps made for X11, which run through XWayland, are drawn at the display's
    // own pixels, sharp, rather than scaled up by Hyprland, blurred. GTK's apps then scale
    // themselves by GDK_SCALE, which takes only whole numbers.
    struct SharpApps {
        bool               on = false;
        std::optional<int> gtkScale;

        friend bool operator==(const SharpApps&, const SharpApps&) = default;
    };

    // What the config sets: xwayland.force_zero_scaling, and GDK_SCALE. Nothing for a
    // config that is not Lua, or that cannot be read.
    [[nodiscard]] std::optional<SharpApps> currentSharpApps(const configuration::MainConfig& config);

    // The sharp apps that suit displays set as `wanted`: GTK's scale is the largest
    // display's, rounded down, when that is 2 or more, since GTK cannot scale by a part.
    [[nodiscard]] SharpApps sharpAppsFor(bool on, const std::vector<DisplaySettings>& wanted);

    // The change to the Hyprland config (a Lua one; a linked config is followed) that sets
    // each display as `wanted` says, in the same order as `displays`: the hl.monitor call
    // for it changed where only what differs is written, or one of its own added for it,
    // by its description. With `sharp`, the XWayland setting and GDK_SCALE too, where they
    // differ; turned off, GDK_SCALE goes back to 1 if the config sets it. Nothing when
    // nothing differs.
    [[nodiscard]] std::expected<std::optional<configuration::FileChange>, std::string> planDisplays(const std::vector<Display>& displays, const std::vector<DisplaySettings>& wanted,
                                                                                                   const std::optional<SharpApps>& sharp, const configuration::MainConfig& config);

}
