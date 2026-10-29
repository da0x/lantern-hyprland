// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace ipc {

    // A monitor, in the layout's pixels.
    struct Monitor {
        int         id = 0;
        std::string name;
        std::string description;
        int         x = 0, y = 0;
        // In its own pixels; the layout has them divided by the scale.
        int         width = 0, height = 0;
        double      refreshRate = 0;
        double      scale       = 1;
        bool        focused     = false;
        bool        disabled    = false;
        int         activeWorkspace  = 0;
        // 0 when no special workspace is shown on it.
        int         specialWorkspace = 0;

        friend bool operator==(const Monitor&, const Monitor&) = default;
    };

    struct Workspace {
        int         id = 0;
        std::string name;
        std::string monitor;
        int         windows       = 0;
        bool        hasFullscreen = false;

        friend bool operator==(const Workspace&, const Workspace&) = default;
    };

    struct Window {
        std::string address;
        std::string windowClass;
        std::string title;
        int         workspace = 0;
        int         monitor   = 0;
        int         x = 0, y = 0, width = 0, height = 0;
        bool        floating   = false;
        bool        fullscreen = false;
        bool        pinned     = false;
        bool        xwayland   = false;
        bool        hidden     = false;
        int         pid        = 0;
        // 0 for the focused window, then in the order they had focus.
        int         focusOrder = 0;

        friend bool operator==(const Window&, const Window&) = default;
    };

    // Everything Hyprland has right now.
    struct Tree {
        std::vector<Monitor>   monitors;
        std::vector<Workspace> workspaces;
        std::vector<Window>    windows;

        friend bool operator==(const Tree&, const Tree&) = default;
    };

    // Reads the replies to Query::monitors, Query::workspaces and Query::clients.
    [[nodiscard]] std::expected<Tree, std::string> parseTree(std::string_view monitors, std::string_view workspaces, std::string_view clients);

}
