// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "configuration/file-change.hpp"
#include "foundation/environment.hpp"

#include <array>
#include <expected>
#include <filesystem>
#include <optional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace desktop {

    // The colors of a Waybar style, as #rrggbb. Each comes from a published palette.
    struct Palette {
        std::string_view name;
        bool             dark = true;
        // The bar itself.
        std::string_view base;
        // Text on the accent.
        std::string_view onAccent;
        // Borders and the hovered workspace.
        std::string_view surface;
        // Empty workspaces and the tooltip's border.
        std::string_view raised;
        // Muted audio.
        std::string_view muted;
        // Workspaces with windows, and the calendar's weekdays.
        std::string_view subtle;
        std::string_view text;
        // The active workspace.
        std::string_view accent;
        std::string_view audio;
        std::string_view network;
        // Urgent workspaces, no network, and today in the calendar.
        std::string_view alert;
        // The nearest of GNOME's accent colors, for GTK apps.
        std::string_view                 gnomeAccent;
    };

    // How the bar sits on the screen.
    enum class Shape {
        // Three rounded groups that float apart: workspaces, the clock, the rest.
        islands,
        // One rounded bar that floats, as far from the edges as the windows.
        floating,
        // A solid bar from edge to edge.
        fullWidth,
    };

    struct ShapeInfo {
        Shape            shape;
        std::string_view name;
        std::string_view summary;
    };

    [[nodiscard]] std::span<const Palette>   waybarPalettes();
    [[nodiscard]] std::span<const ShapeInfo> waybarShapes();

    // How round the bar is and how far it sits from the screen's edges: the windows' own,
    // set on the Windows page, so that the bar lines up with them. The defaults are what
    // Lantern's styles had before they followed the windows.
    struct BarGeometry {
        // The corners of the bar, or of its islands; the workspace buttons' follow.
        int rounding = 10;
        // From the sides, and from the top.
        int edgeGap  = 20;
        // From the top: the edge gap when the bar follows the windows; half of it in
        // files Lantern wrote before.
        int topGap   = 10;
        // The windows' border width, for a bar with their border.
        int border   = 1;

        friend bool operator==(const BarGeometry&, const BarGeometry&) = default;
    };

    // The bar's border, as wide as the windows' and in the color of an inactive or of the
    // focused window's. `thin` is the faint line styles had before, still read in files
    // written then.
    enum class Border {
        thin,
        inactive,
        active,
    };

    // What the bar shows. Each is one of Waybar's own modules, so none needs a program of its
    // own. In the order they sit on each side of the bar.
    enum class Widget {
        workspaces,
        windowTitle,
        clock,
        // The temperature from wttr.in, which sees the computer's address and so roughly
        // where it is: the one widget that goes out to the internet.
        weather,
        media,
        privacy,
        cpu,
        memory,
        temperature,
        keepAwake,
        bluetooth,
        battery,
        sound,
        network,
        tray,
    };

    // Where a widget sits on the bar.
    enum class Side {
        left,
        center,
        right,
    };

    struct WidgetInfo {
        Widget           widget;
        // Waybar's name for the module.
        std::string_view module;
        std::string_view name;
        std::string_view summary;
        // Where it goes when first turned on.
        Side             side;
        // Whether a new bar has it: the workspaces, the clock, the sound, the network and
        // the tray, as in Waybar's own config.
        bool             standard;
    };

    [[nodiscard]] std::span<const WidgetInfo> waybarWidgets();

    // The widgets on each side of the bar, indexed by Side, in the order they are shown.
    using WidgetSides = std::array<std::vector<Widget>, 3>;

    // The widgets a new bar has, each on its own side.
    [[nodiscard]] WidgetSides standardWidgets();

    // The side a widget is on, if it is on the bar.
    [[nodiscard]] std::optional<Side> sideOf(const WidgetSides& sides, Widget widget);

    // Takes a widget off the bar.
    void removeWidget(WidgetSides& sides, Widget widget);

    // Puts a widget on `side`, before `before` when that is there, or at the end; from
    // wherever it was.
    void placeWidget(WidgetSides& sides, Widget widget, Side side, std::optional<Widget> before = std::nullopt);

    // Moves a widget `by` places along its side, toward the start when negative, and no
    // further than either end.
    void moveWidget(WidgetSides& sides, Widget widget, int by);

    // The widgets a Waybar config shows, by the modules it lists, on their sides in order.
    [[nodiscard]] WidgetSides widgetsIn(std::string_view config);

    // A complete style: the shape, the palette, the geometry, the border, and the widgets.
    struct WaybarStyle {
        Shape          shape    = Shape::islands;
        const Palette* palette  = nullptr;
        BarGeometry    geometry = {};
        Border         border   = Border::thin;
        WidgetSides    widgets  = standardWidgets();

        bool operator==(const WaybarStyle&) const = default;
    };

    // Waybar's config and stylesheet for the style: plain Waybar files with nothing of Lantern's
    // in them.
    [[nodiscard]] std::string waybarConfigText(const WaybarStyle& style);
    [[nodiscard]] std::string waybarStyleText(const WaybarStyle& style);

    // The folder Lantern writes Waybar's files to: the user's folder Waybar reads now,
    // or, when it reads the system's example, $XDG_CONFIG_HOME/waybar or ~/.config/waybar.
    [[nodiscard]] std::filesystem::path waybarUserFolder(const foundation::Environment& environment, const std::filesystem::path& systemFolder = "/etc/xdg");

    // The config and the stylesheet that `style` would write, with what is there now.
    [[nodiscard]] std::expected<std::vector<configuration::FileChange>, std::string> planWaybarStyle(const WaybarStyle& style, const foundation::Environment& environment,
                                                                                                    const std::filesystem::path& systemFolder = "/etc/xdg");

    // Lantern's style that Waybar's files hold now, if they hold one exactly, with any
    // border and the widgets its config lists.
    // Only styles with `geometry` are looked for, since the files hold it too.
    [[nodiscard]] std::optional<WaybarStyle> currentWaybarStyle(const foundation::Environment& environment, const BarGeometry& geometry = {},
                                                                const std::filesystem::path& systemFolder = "/etc/xdg");

}
