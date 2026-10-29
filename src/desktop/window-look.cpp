// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/window-look.hpp"

#include "configuration/lua-edit.hpp"
#include "syntax/highlight.hpp"

#include <array>
#include <charconv>
#include <format>
#include <fstream>
#include <functional>
#include <sstream>
#include <string_view>
#include <vector>

namespace desktop {

    namespace {

        // Each value: where hl.config keeps it, and the member it is.
        struct Field {
            std::vector<std::string_view> path;
            int WindowLook::*             number = nullptr;
        };

        const std::array<Field, 4> numbers{{
            {{"decoration", "rounding"}, &WindowLook::rounding},
            {{"general", "gaps_in"}, &WindowLook::gapsIn},
            {{"general", "gaps_out"}, &WindowLook::gapsOut},
            {{"general", "border_size"}, &WindowLook::borderSize},
        }};
        const std::vector<std::string_view> resizePath{"general", "resize_on_border"};
        const std::vector<std::string_view> blurPath{"decoration", "blur", "enabled"};

        // Hyprland's own defaults, for values a config leaves out.
        constexpr WindowLook hyprlandDefaults{.rounding = 0, .gapsIn = 5, .gapsOut = 20, .borderSize = 1, .resizeOnBorder = false, .blur = true, .floatFilePickers = false};

        constexpr std::string_view pickerRule = "hl.window_rule({\n"
                                                "    -- Open and save dialogs, which the portal shows for other apps.\n"
                                                "    name   = \"float-file-pickers\",\n"
                                                "    match  = { class = \"^xdg-desktop-portal-gtk$\" },\n"
                                                "\n"
                                                "    float  = true,\n"
                                                "    center = true,\n"
                                                "})\n";

        std::optional<std::string> luaValue(std::string_view config, const std::vector<std::string_view>& path) {
            const auto found = syntax::luaCallField(config, "hl.config", path);
            if (!found)
                return std::nullopt;
            return std::string{config.substr(found->begin, found->end - found->begin)};
        }

        // 5, or "5 10 5 10", whose first side is taken.
        std::optional<int> numberOf(std::string_view value) {
            if (value.size() >= 2 && value.front() == '"')
                value = value.substr(1, value.size() - 2);
            int        number = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
            if (error != std::errc{} || (end != value.data() + value.size() && *end != ' '))
                return std::nullopt;
            return number;
        }

    }

    std::optional<WindowLook> currentWindowLook(const configuration::MainConfig& config) {
        if (!config.lua())
            return std::nullopt;
        std::ifstream stream{config.file, std::ios::binary};
        if (!stream)
            return std::nullopt;
        std::stringstream text;
        text << stream.rdbuf();
        WindowLook look = hyprlandDefaults;
        for (const auto& field : numbers)
            if (const auto value = luaValue(text.str(), field.path))
                look.*field.number = numberOf(*value).value_or(look.*field.number);
        if (const auto value = luaValue(text.str(), resizePath))
            look.resizeOnBorder = *value == "true";
        if (const auto value = luaValue(text.str(), blurPath))
            look.blur = *value != "false";
        look.floatFilePickers = configuration::hasLuaWindowRule(text.str(), "float-file-pickers");
        return look;
    }

    BarGeometry barGeometry(const std::optional<WindowLook>& look) {
        if (!look)
            return {};
        return {.rounding = look->rounding, .edgeGap = look->gapsOut, .topGap = look->gapsOut, .border = look->borderSize};
    }

    BarGeometry barGeometry(const configuration::MainConfig& config) {
        return barGeometry(currentWindowLook(config));
    }

    BarGeometry runningBarGeometry() {
        const auto config = configuration::mainConfig(configuration::lanternLaunch());
        return config ? barGeometry(*config) : BarGeometry{};
    }

    std::optional<WaybarStyle> currentBarStyle(const foundation::Environment& environment, const BarGeometry& geometry) {
        // Files written before have half the gap at the top, or, before the bar followed
        // the windows, the old fixed geometry.
        auto style = currentWaybarStyle(environment, geometry);
        if (!style)
            style = currentWaybarStyle(environment, BarGeometry{.rounding = geometry.rounding, .edgeGap = geometry.edgeGap, .topGap = geometry.edgeGap / 2, .border = geometry.border});
        if (!style)
            style = currentWaybarStyle(environment, BarGeometry{});
        if (style)
            style->geometry = geometry;
        return style;
    }

    std::expected<std::vector<configuration::FileChange>, std::string> planBarFollowing(const WindowLook& wanted, const std::optional<WindowLook>& current,
                                                                                       const foundation::Environment& environment) {
        auto style = currentBarStyle(environment, barGeometry(current));
        if (!style)
            return std::vector<configuration::FileChange>{};
        style->geometry = barGeometry(wanted);
        auto planned    = planWaybarStyle(*style, environment);
        if (planned)
            std::erase_if(*planned, [](const auto& change) { return !change.changes(); });
        return planned;
    }

    std::expected<std::optional<configuration::FileChange>, std::string> planWindowLook(const WindowLook& wanted, const configuration::MainConfig& config) {
        if (!config.lua())
            return std::unexpected{"windows are set only in a Lua config (hyprland.lua) so far"};
        const auto current = currentWindowLook(config);
        return configuration::planEdit(config.file, [&wanted, &current](std::string_view config) {
            auto       text = std::string{config};
            const auto set  = [&text](const std::vector<std::string_view>& path, const std::string& value, bool differs) {
                // Only what changes, so a value the config leaves to Hyprland stays left.
                if (differs)
                    text = configuration::withLuaConfigValueSet(text, path, value);
            };
            for (const auto& field : numbers)
                set(field.path, std::to_string(wanted.*field.number), !current || (*current).*field.number != wanted.*field.number);
            set(resizePath, wanted.resizeOnBorder ? "true" : "false", !current || current->resizeOnBorder != wanted.resizeOnBorder);
            set(blurPath, wanted.blur ? "true" : "false", !current || current->blur != wanted.blur);
            return configuration::withLuaWindowRule(text, "float-file-pickers", pickerRule, wanted.floatFilePickers);
        });
    }

}
