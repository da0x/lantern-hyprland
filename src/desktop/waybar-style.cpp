// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/waybar-style.hpp"
#include "desktop/user-themes.hpp"

#include "desktop/waybar.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>

namespace desktop {

    namespace {

        // The palettes' colors as their authors publish them: Catppuccin, Rosé Pine,
        // Tokyo Night, Nord and Gruvbox are all under the MIT license.
        constexpr std::array palettes{
            Palette{.name = "Catppuccin Mocha", .dark = true, .base = "#1e1e2e", .onAccent = "#11111b", .surface = "#313244", .raised = "#45475a", .muted = "#6c7086",
                    .subtle = "#a6adc8", .text = "#cdd6f4", .accent = "#cba6f7", .audio = "#89b4fa", .network = "#94e2d5", .alert = "#f38ba8", .gnomeAccent = "purple"},
            Palette{.name = "Catppuccin Latte", .dark = false, .base = "#eff1f5", .onAccent = "#eff1f5", .surface = "#ccd0da", .raised = "#bcc0cc", .muted = "#9ca0b0",
                    .subtle = "#6c6f85", .text = "#4c4f69", .accent = "#8839ef", .audio = "#1e66f5", .network = "#179299", .alert = "#d20f39", .gnomeAccent = "purple"},
            Palette{.name = "Rosé Pine", .dark = true, .base = "#191724", .onAccent = "#191724", .surface = "#26233a", .raised = "#403d52", .muted = "#6e6a86",
                    .subtle = "#908caa", .text = "#e0def4", .accent = "#c4a7e7", .audio = "#9ccfd8", .network = "#ebbcba", .alert = "#eb6f92", .gnomeAccent = "purple"},
            Palette{.name = "Tokyo Night", .dark = true, .base = "#1a1b26", .onAccent = "#1a1b26", .surface = "#292e42", .raised = "#3b4261", .muted = "#565f89",
                    .subtle = "#a9b1d6", .text = "#c0caf5", .accent = "#7aa2f7", .audio = "#7dcfff", .network = "#9ece6a", .alert = "#f7768e", .gnomeAccent = "blue"},
            Palette{.name = "Nord", .dark = true, .base = "#2e3440", .onAccent = "#2e3440", .surface = "#3b4252", .raised = "#4c566a", .muted = "#616e88",
                    .subtle = "#d8dee9", .text = "#eceff4", .accent = "#88c0d0", .audio = "#81a1c1", .network = "#a3be8c", .alert = "#bf616a", .gnomeAccent = "teal"},
            Palette{.name = "Gruvbox Dark", .dark = true, .base = "#282828", .onAccent = "#282828", .surface = "#3c3836", .raised = "#504945", .muted = "#7c6f64",
                    .subtle = "#a89984", .text = "#ebdbb2", .accent = "#fabd2f", .audio = "#83a598", .network = "#8ec07c", .alert = "#fb4934", .gnomeAccent = "yellow"},
        };

        constexpr std::array shapes{
            ShapeInfo{Shape::islands, "Islands", "Three rounded groups that float apart."},
            ShapeInfo{Shape::floating, "Floating", "One rounded bar, as far from the edges as your windows."},
            ShapeInfo{Shape::fullWidth, "Full width", "A solid bar from edge to edge."},
        };

        std::string_view shapeName(Shape shape) {
            for (const auto& info : shapes)
                if (info.shape == shape)
                    return info.name;
            return {};
        }

        bool under(const std::filesystem::path& file, const std::filesystem::path& folder) {
            const auto relative = file.lexically_relative(folder);
            return !relative.empty() && !relative.native().starts_with("..");
        }

        // What every shape shares: the font, the workspaces and the modules' colors.
        constexpr std::string_view moduleRules = R"(
#workspaces button {
    padding: 0 10px;
    margin: 5px 2px;
    border-radius: 7px;
    color: @subtle;
    background: transparent;
    background-image: none;
    box-shadow: none;
    text-shadow: none;
}

#workspaces button.empty {
    color: @raised;
}

#workspaces button:hover {
    background: @surface;
    color: @text;
}

#workspaces button.active {
    background: @accent;
    color: @on_accent;
}

#workspaces button.urgent {
    background: @alert;
    color: @on_accent;
}

#clock {
    padding: 0 12px;
    font-weight: 600;
}

#pulseaudio,
#network,
#tray {
    padding: 0 10px;
}

#pulseaudio {
    color: @audio;
}

#pulseaudio.muted {
    color: @muted;
}

#network {
    color: @network;
}

#network.disconnected {
    color: @alert;
}

#tray > .needs-attention {
    background: @alert;
    border-radius: 7px;
}

tooltip {
    background: @base;
    border: 1px solid @raised;
    border-radius: 8px;
}

tooltip label {
    color: @text;
}
)";

        constexpr std::array widgets{
            WidgetInfo{Widget::workspaces, "hyprland/workspaces", "Workspaces", "Your workspaces, to go to one.", Side::left, true},
            WidgetInfo{Widget::windowTitle, "hyprland/window", "Window title", "The window you are using.", Side::left, false},
            WidgetInfo{Widget::clock, "clock", "Clock", "Date and time; a calendar on hover.", Side::center, true},
            WidgetInfo{Widget::weather, "custom/weather", "Weather", "The temperature, from wttr.in.", Side::right, false},
            WidgetInfo{Widget::media, "mpris", "Media", "What is playing.", Side::right, false},
            WidgetInfo{Widget::privacy, "privacy", "Privacy", "A sign while your screen or mic is shared.", Side::right, false},
            WidgetInfo{Widget::cpu, "cpu", "Processor", "How busy the processor is.", Side::right, false},
            WidgetInfo{Widget::memory, "memory", "Memory", "How much memory is in use.", Side::right, false},
            WidgetInfo{Widget::temperature, "temperature", "Temperature", "How warm the processor is.", Side::right, false},
            WidgetInfo{Widget::keepAwake, "idle_inhibitor", "Keep awake", "Keeps the screen from sleeping.", Side::right, false},
            WidgetInfo{Widget::bluetooth, "bluetooth", "Bluetooth", "Whether it is on, and what is connected.", Side::right, false},
            WidgetInfo{Widget::battery, "battery", "Battery", "How much charge is left.", Side::right, false},
            WidgetInfo{Widget::sound, "pulseaudio", "Sound", "The volume, and its control on a click.", Side::right, true},
            WidgetInfo{Widget::network, "network", "Network", "The connection, and its settings.", Side::right, true},
            WidgetInfo{Widget::tray, "tray", "Tray", "Icons that programs put there.", Side::right, true},
        };

        const WidgetInfo& widgetInfo(Widget widget) {
            return *std::ranges::find(widgets, widget, &WidgetInfo::widget);
        }

        // Each widget's settings in Waybar's config, ending in a comma like the blocks around it.
        std::string widgetBlock(Widget widget) {
            switch (widget) {
                // Always in the config, where they have always been.
                case Widget::workspaces:
                case Widget::clock:
                case Widget::sound:
                case Widget::network:
                case Widget::tray: return {};
                case Widget::windowTitle:
                    return R"(    "hyprland/window": {
        "format": "{title}",
        "max-length": 48,
        "separate-outputs": true
    },
)";
                case Widget::weather:
                    // Asked every half hour; curl prints nothing when the site cannot be
                    // reached, and Waybar hides a custom module with nothing to show.
                    return std::format(R"(    "custom/weather": {{
        "exec": "curl -sf --max-time 10 'https://wttr.in/?format=%t'",
        "interval": 1800,
        "format": "{}  {{}}",
        "tooltip": false,
        "on-click": "xdg-open https://wttr.in"
    }},
)",
                                       "\U000F0595");
                case Widget::media:
                    return std::format(R"(    "mpris": {{
        "format": "{}  {{dynamic}}",
        "format-paused": "{}  {{dynamic}}",
        "dynamic-len": 36,
        "dynamic-order": ["title", "artist"]
    }},
)",
                                       "\U000F075A", "\U000F03E4");
                case Widget::privacy:
                    return R"(    "privacy": {
        "icon-size": 16,
        "icon-spacing": 6
    },
)";
                case Widget::cpu:
                    return std::format(R"(    "cpu": {{
        "format": "{}  {{usage}}%",
        "interval": 5
    }},
)",
                                       "\U000F061A");
                case Widget::memory:
                    return std::format(R"(    "memory": {{
        "format": "{}  {{percentage}}%",
        "interval": 10,
        "tooltip-format": "{{used:0.1f}} of {{total:0.1f}} GiB in use"
    }},
)",
                                       "\U000F035B");
                case Widget::temperature:
                    return std::format(R"(    "temperature": {{
        "format": "{}  {{temperatureC}}°C",
        "critical-threshold": 85
    }},
)",
                                       "\U000F050F");
                case Widget::keepAwake:
                    return std::format(R"(    "idle_inhibitor": {{
        "format": "{{icon}}",
        "format-icons": {{
            "activated": "{}",
            "deactivated": "{}"
        }},
        "tooltip-format-activated": "The screen stays awake",
        "tooltip-format-deactivated": "The screen sleeps as usual"
    }},
)",
                                       "\U000F0208", "\U000F0209");
                case Widget::bluetooth:
                    return std::format(R"(    "bluetooth": {{
        "format": "{}",
        "format-off": "{}",
        "format-disabled": "{}",
        "format-connected": "{}  {{device_alias}}",
        "tooltip-format-connected": "{{device_enumerate}}"
    }},
)",
                                       "\U000F00AF", "\U000F00B2", "\U000F00B2", "\U000F00AF");
                case Widget::battery:
                    return std::format(R"(    "battery": {{
        "format": "{{icon}}  {{capacity}}%",
        "format-charging": "{}  {{capacity}}%",
        "format-icons": [
            "{}",
            "{}",
            "{}",
            "{}",
            "{}"
        ],
        "states": {{
            "warning": 20,
            "critical": 10
        }}
    }},
)",
                                       "\U000F0084", "\U000F007A", "\U000F007C", "\U000F007E", "\U000F0080", "\U000F0082");
            }
            return {};
        }

        // Each widget's look, in the palette's colors.
        std::string_view widgetRules(Widget widget) {
            switch (widget) {
                // Always in the stylesheet.
                case Widget::workspaces:
                case Widget::clock:
                case Widget::sound:
                case Widget::network:
                case Widget::tray: return {};
                case Widget::windowTitle:
                    return R"(
#window {
    padding: 0 10px;
    color: @subtle;
}
)";
                case Widget::weather:
                    return R"(
#custom-weather {
    padding: 0 10px;
    color: @text;
}
)";
                case Widget::media:
                    return R"(
#mpris {
    padding: 0 10px;
    color: @text;
}

#mpris.paused {
    color: @muted;
}
)";
                case Widget::privacy:
                    return R"(
#privacy {
    padding: 0 10px;
}

#privacy-item {
    color: @alert;
}
)";
                case Widget::cpu:
                    return R"(
#cpu {
    padding: 0 10px;
    color: @subtle;
}
)";
                case Widget::memory:
                    return R"(
#memory {
    padding: 0 10px;
    color: @subtle;
}
)";
                case Widget::temperature:
                    return R"(
#temperature {
    padding: 0 10px;
    color: @subtle;
}

#temperature.critical {
    color: @alert;
}
)";
                case Widget::keepAwake:
                    return R"(
#idle_inhibitor {
    padding: 0 10px;
    color: @muted;
}

#idle_inhibitor.activated {
    color: @accent;
}
)";
                case Widget::bluetooth:
                    return R"(
#bluetooth {
    padding: 0 10px;
    color: @audio;
}

#bluetooth.off,
#bluetooth.disabled {
    color: @muted;
}
)";
                case Widget::battery:
                    return R"(
#battery {
    padding: 0 10px;
    color: @text;
}

#battery.charging {
    color: @network;
}

#battery.warning:not(.charging) {
    color: @alert;
}
)";
            }
            return {};
        }

        // A JSON array of module names, laid out as the config's other arrays.
        std::string moduleList(const std::vector<std::string_view>& names) {
            std::string list = "[\n";
            for (std::size_t index = 0; index < names.size(); ++index)
                list += std::format("        \"{}\"{}\n", names[index], index + 1 < names.size() ? "," : "");
            return list + "    ]";
        }

        std::string_view shapeRules(Shape shape) {
            switch (shape) {
                case Shape::islands:
                    return R"(
window#waybar {
    background: transparent;
    color: @text;
}

.modules-left,
.modules-center,
.modules-right {
    background: alpha(@base, 0.92);
    border: 1px solid @surface;
    border-radius: 10px;
    padding: 0 6px;
}
)";
                case Shape::floating:
                    return R"(
window#waybar {
    background: alpha(@base, 0.92);
    color: @text;
    border: 1px solid @surface;
    border-radius: 10px;
}

.modules-left,
.modules-right {
    padding: 0 6px;
}
)";
                case Shape::fullWidth:
                    return R"(
window#waybar {
    background: @base;
    color: @text;
    border-bottom: 1px solid @surface;
}

.modules-left,
.modules-right {
    padding: 0 6px;
}
)";
            }
            return {};
        }

    }

    std::span<const Palette> waybarPalettes() {
        return palettes;
    }

    std::span<const ShapeInfo> waybarShapes() {
        return shapes;
    }

    std::span<const WidgetInfo> waybarWidgets() {
        return widgets;
    }

    WidgetSides standardWidgets() {
        WidgetSides standard;
        for (const auto& info : widgets)
            if (info.standard)
                standard[static_cast<std::size_t>(info.side)].push_back(info.widget);
        return standard;
    }

    std::optional<Side> sideOf(const WidgetSides& sides, Widget widget) {
        for (std::size_t side = 0; side < sides.size(); ++side)
            if (std::ranges::contains(sides[side], widget))
                return static_cast<Side>(side);
        return std::nullopt;
    }

    void removeWidget(WidgetSides& sides, Widget widget) {
        for (auto& side : sides)
            std::erase(side, widget);
    }

    void placeWidget(WidgetSides& sides, Widget widget, Side side, std::optional<Widget> before) {
        removeWidget(sides, widget);
        auto&      list = sides[static_cast<std::size_t>(side)];
        const auto at   = before ? std::ranges::find(list, *before) : list.end();
        list.insert(at, widget);
    }

    void moveWidget(WidgetSides& sides, Widget widget, int by) {
        for (auto& list : sides) {
            const auto at = std::ranges::find(list, widget);
            if (at == list.end())
                continue;
            const auto to = std::clamp<std::ptrdiff_t>((at - list.begin()) + by, 0, static_cast<std::ptrdiff_t>(list.size()) - 1);
            list.erase(at);
            list.insert(list.begin() + to, widget);
            return;
        }
    }

    WidgetSides widgetsIn(std::string_view config) {
        WidgetSides found;
        const auto       parsed = nlohmann::json::parse(config, nullptr, false, true);
        if (parsed.is_discarded() || !parsed.is_object())
            return found;
        for (const auto& [key, side] : {std::pair{"modules-left", Side::left}, std::pair{"modules-center", Side::center}, std::pair{"modules-right", Side::right}}) {
            if (!parsed.contains(key) || !parsed[key].is_array())
                continue;
            for (const auto& module : parsed[key])
                if (module.is_string())
                    for (const auto& info : widgets)
                        if (module.get<std::string>() == info.module && !sideOf(found, info.widget))
                            found[static_cast<std::size_t>(side)].push_back(info.widget);
        }
        return found;
    }

    std::string waybarConfigText(const WaybarStyle& style) {
        const auto& palette  = *style.palette;
        const bool  floating = style.shape != Shape::fullWidth;
        // Each side lists its widgets in the order chosen. Their settings follow in the order
        // of Widget, so that moving one changes only the lists.
        std::array<std::vector<std::string_view>, 3> lists;
        for (std::size_t side = 0; side < lists.size(); ++side)
            for (const auto widget : style.widgets[side])
                lists[side].push_back(widgetInfo(widget).module);
        const auto& [left, center, right] = lists;
        std::string blocks;
        for (const auto& info : widgets)
            if (sideOf(style.widgets, info.widget))
                blocks += widgetBlock(info.widget);
        return std::format(R"({{
    "layer": "top",
    "position": "top",
    "height": {},
    "margin-top": {},
    "margin-left": {},
    "margin-right": {},
    "spacing": 8,
    "reload_style_on_change": true,
    "modules-left": {},
    "modules-center": {},
    "modules-right": {},
    "hyprland/workspaces": {{
        "format": "{{name}}",
        "on-click": "activate",
        "sort-by-number": true,
        "persistent-workspaces": {{
            "*": 5
        }}
    }},
    "clock": {{
        "format": "{{:%a %d %b   %H:%M}}",
        "tooltip-format": "<tt>{{calendar}}</tt>",
        "calendar": {{
            "mode": "month",
            "format": {{
                "months": "<span color='{}'><b>{{}}</b></span>",
                "weekdays": "<span color='{}'>{{}}</span>",
                "today": "<span color='{}'><b><u>{{}}</u></b></span>"
            }}
        }}
    }},
    "pulseaudio": {{
        "format": "{{icon}}  {{volume}}%",
        "format-muted": "{}  muted",
        "format-icons": {{
            "headphone": "{}",
            "default": [
                "{}",
                "{}",
                "{}"
            ]
        }},
        "scroll-step": 5,
        "on-click": "pavucontrol",
        "on-click-right": "wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle"
    }},
    "network": {{
        "format-wifi": "{}  {{essid}}",
        "format-ethernet": "{}  wired",
        "format-disconnected": "{}  offline",
        "tooltip-format": "{{ifname}}  {{ipaddr}}/{{cidr}}",
        "on-click": "nm-connection-editor"
    }},
{}    "tray": {{
        "icon-size": 18,
        "spacing": 10
    }}
}}
)",
                           floating ? 38 : 34, floating ? style.geometry.topGap : 0, floating ? style.geometry.edgeGap : 0, floating ? style.geometry.edgeGap : 0, moduleList(left),
                           moduleList(center), moduleList(right), palette.accent, palette.subtle, palette.alert, "\U000F075F", "\U000F02CB", "\U000F057F", "\U000F0580", "\U000F057E", "\U000F0928",
                           "\U000F0200", "\U000F092D", blocks);
    }

    std::string waybarStyleText(const WaybarStyle& style) {
        const auto& palette = *style.palette;
        auto        text    = std::format(R"(/* {}, {}. */

@define-color base      {};
@define-color on_accent {};
@define-color surface   {};
@define-color raised    {};
@define-color muted     {};
@define-color subtle    {};
@define-color text      {};
@define-color accent    {};
@define-color audio     {};
@define-color network   {};
@define-color alert     {};

* {{
    font-family: "Adwaita Sans", "Symbols Nerd Font";
    font-size: 15px;
    min-height: 0;
    border: none;
    border-radius: 0;
}}
)",
                                      palette.name, shapeName(style.shape), palette.base, palette.onAccent, palette.surface, palette.raised, palette.muted, palette.subtle,
                                      palette.text, palette.accent, palette.audio, palette.network, palette.alert);
        text += shapeRules(style.shape);
        text += moduleRules;
        for (const auto& info : widgets)
            if (sideOf(style.widgets, info.widget))
                text += widgetRules(info.widget);
        // The radii written above are the default geometry's: the bar's 10 and the
        // workspace buttons' 7, which keep that proportion to it.
        const auto replaced = [&text](std::string_view from, int pixels) {
            const auto to = std::format("border-radius: {}px;", pixels);
            for (auto at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
                text.replace(at, from.size(), to);
        };
        replaced("border-radius: 10px;", style.geometry.rounding);
        replaced("border-radius: 7px;", static_cast<int>(std::lround(style.geometry.rounding * 0.7)));
        if (style.border != Border::thin) {
            const std::string_view from = "1px solid @surface;";
            const auto             to   = std::format("{}px solid alpha(@{}, {});", style.geometry.border, style.border == Border::active ? "accent" : "raised",
                                                      style.border == Border::active ? "0.93" : "0.67");
            for (auto at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
                text.replace(at, from.size(), to);
            // GTK draws a border image with square corners, so only a square bar takes the
            // gradient.
            if (style.border == Border::active && style.geometry.rounding == 0)
                text += std::format(R"(
{} {{
    border-image: linear-gradient(45deg, alpha(@accent, 0.93), alpha(@audio, 0.93)) 1;
}}
)",
                                    style.shape == Shape::islands ? ".modules-left,\n.modules-center,\n.modules-right" : "window#waybar");
        }
        return text;
    }

    std::filesystem::path waybarUserFolder(const foundation::Environment& environment, const std::filesystem::path& systemFolder) {
        if (const auto config = waybarConfig(environment, systemFolder); config && !under(*config, systemFolder))
            return config->parent_path();
        if (const auto home = foundation::variable(environment, "XDG_CONFIG_HOME"); home && std::filesystem::path{*home}.is_absolute())
            return std::filesystem::path{*home} / "waybar";
        return std::filesystem::path{foundation::variable(environment, "HOME").value_or("")} / ".config" / "waybar";
    }

    std::expected<std::vector<configuration::FileChange>, std::string> planWaybarStyle(const WaybarStyle& style, const foundation::Environment& environment,
                                                                                      const std::filesystem::path& systemFolder) {
        const auto folder = waybarUserFolder(environment, systemFolder);
        // Keep the name of a config that is already there: Waybar reads config before config.jsonc.
        auto configFile = folder / "config.jsonc";
        if (const auto config = waybarConfig(environment, systemFolder); config && config->parent_path() == folder)
            configFile = *config;

        std::vector<configuration::FileChange> changes;
        for (auto& [file, text] : {std::pair{configFile, waybarConfigText(style)}, std::pair{folder / "style.css", waybarStyleText(style)}}) {
            auto change = configuration::planFile(file, text);
            if (!change)
                return std::unexpected{change.error()};
            changes.push_back(std::move(*change));
        }
        return changes;
    }

    std::optional<WaybarStyle> currentWaybarStyle(const foundation::Environment& environment, const BarGeometry& geometry, const std::filesystem::path& systemFolder) {
        // The widgets are read from the config, rather than tried in every combination.
        WidgetSides placed;
        if (const auto config = waybarConfig(environment, systemFolder)) {
            std::ifstream     stream{*config, std::ios::binary};
            std::stringstream text;
            text << stream.rdbuf();
            placed = widgetsIn(text.str());
        }
        for (const auto& info : shapes) {
            for (const auto* palette : allPalettes()) {
                for (const auto border : {Border::thin, Border::inactive, Border::active}) {
                    const WaybarStyle style{info.shape, palette, geometry, border, placed};
                    const auto        planned = planWaybarStyle(style, environment, systemFolder);
                    if (planned && std::ranges::none_of(*planned, [](const auto& change) { return change.changes(); }))
                        return style;
                }
            }
        }
        return std::nullopt;
    }

}
