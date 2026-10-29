// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "configuration/file-change.hpp"
#include "configuration/main-config.hpp"
#include "desktop/waybar-style.hpp"
#include "foundation/environment.hpp"

#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace desktop {

    // How windows look and are handled: their corners, the gaps around them, their border,
    // whether dragging that border resizes them, and whether what is behind a see-through
    // window is blurred. Each is a value in hl.config.
    struct WindowLook {
        // Corner radius, in pixels.
        int  rounding       = 10;
        // Between windows, on each window's side, so two windows are twice this apart.
        int  gapsIn         = 5;
        // Between the windows and the screen's edges.
        int  gapsOut        = 20;
        int  borderSize     = 2;
        // Dragging a window's border or gap resizes it, as on other desktops; holding Super
        // and dragging with the right button resizes it either way.
        bool resizeOnBorder = false;
        // What shows through a see-through window, such as a terminal, is blurred.
        bool blur           = true;
        // The open and save dialogs that xdg-desktop-portal-gtk shows for other apps float
        // in the middle of the screen. Hyprland floats an app's own dialogs, but tiles
        // these: they come from another program. A window rule of Lantern's, by its name.
        bool floatFilePickers = false;

        friend bool operator==(const WindowLook&, const WindowLook&) = default;
    };

    // The values the Hyprland config sets, with Hyprland's own default for any it does not
    // set; a gap written as four sides reads as its first. Nothing for a config that is not
    // Lua, or that cannot be read.
    [[nodiscard]] std::optional<WindowLook> currentWindowLook(const configuration::MainConfig& config);

    // The change to the Hyprland config (a Lua one; a linked config is followed) that sets
    // `wanted`, each value where the config writes it or in a call of its own at the end,
    // and Lantern's window rule for file pickers after the config's last window rule, or
    // nothing when it already does.
    [[nodiscard]] std::expected<std::optional<configuration::FileChange>, std::string> planWindowLook(const WindowLook& wanted, const configuration::MainConfig& config);

    // The bar's geometry that lines it up with windows that look like `look`: their corners
    // and their gap from the screen's edges. Lantern's defaults when there is no look.
    [[nodiscard]] BarGeometry barGeometry(const std::optional<WindowLook>& look);

    // The geometry for the config's windows as they are now.
    [[nodiscard]] BarGeometry barGeometry(const configuration::MainConfig& config);

    // The same, for the config the running Hyprland chose.
    [[nodiscard]] BarGeometry runningBarGeometry();

    // Lantern's style that Waybar's files hold, written with `geometry` or with the fixed
    // one styles had before they followed the windows, given back with `geometry`: the style
    // the bar should have now.
    [[nodiscard]] std::optional<WaybarStyle> currentBarStyle(const foundation::Environment& environment, const BarGeometry& geometry);

    // Waybar's files again with `wanted`'s geometry, when they hold one of Lantern's styles
    // with `current`'s, or with the fixed one styles had before they followed the windows;
    // nothing when they hold anything else, which Lantern leaves alone.
    [[nodiscard]] std::expected<std::vector<configuration::FileChange>, std::string> planBarFollowing(const WindowLook& wanted, const std::optional<WindowLook>& current,
                                                                                                     const foundation::Environment& environment);

}
