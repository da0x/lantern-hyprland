// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/displays.hpp"

#include "configuration/lua-edit.hpp"
#include "syntax/highlight.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>
#include <regex>

namespace desktop {

    namespace {

        // A field of the wrong type reads as missing.
        std::string text(const nlohmann::json& object, const char* name) {
            const auto field = object.find(name);
            return field != object.end() && field->is_string() ? field->get<std::string>() : std::string{};
        }

        double number(const nlohmann::json& object, const char* name, double otherwise = 0) {
            const auto field = object.find(name);
            return field != object.end() && field->is_number() ? field->get<double>() : otherwise;
        }

        int whole(const nlohmann::json& object, const char* name) {
            return static_cast<int>(std::lround(number(object, name)));
        }

        bool flag(const nlohmann::json& object, const char* name) {
            const auto field = object.find(name);
            return field != object.end() && field->is_boolean() && field->get<bool>();
        }

        // "1920x1080@60.00Hz"
        std::optional<Mode> modeOf(const std::string& written) {
            static const std::regex pattern{R"((\d+)x(\d+)@([0-9.]+)Hz)"};
            std::smatch             match;
            if (!std::regex_match(written, match, pattern))
                return std::nullopt;
            return Mode{std::stoi(match[1]), std::stoi(match[2]), std::stod(match[3])};
        }

        // Whether a monitor rule's output names `display`: its connector, or "desc:" and the
        // start of its description.
        bool names(std::string_view output, const Display& display) {
            if (output == display.name)
                return true;
            constexpr std::string_view prefix = "desc:";
            if (!output.starts_with(prefix) || display.description.empty())
                return false;
            output.remove_prefix(prefix.size());
            while (!output.empty() && output.front() == ' ')
                output.remove_prefix(1);
            return !output.empty() && std::string_view{display.description}.starts_with(output);
        }

        std::string inQuotes(std::string_view value) {
            return std::format("\"{}\"", value);
        }

        std::string modeValue(const Mode& mode) {
            return inQuotes(mode.width > 0 ? modeText(mode) : "preferred");
        }

        std::string positionValue(const DisplaySettings& settings) {
            return inQuotes(std::format("{}x{}", settings.x, settings.y));
        }

        // Only what differs from how the display is now, for a call the config already has.
        std::vector<configuration::LuaField> differences(const DisplaySettings& now, const DisplaySettings& wanted) {
            std::vector<configuration::LuaField> fields;
            if (!wanted.enabled)
                return {{"disabled", "true"}};
            if (!now.enabled)
                fields.emplace_back("disabled", "false");
            if (wanted.mode != now.mode)
                fields.emplace_back("mode", modeValue(wanted.mode));
            if (wanted.x != now.x || wanted.y != now.y)
                fields.emplace_back("position", positionValue(wanted));
            if (wanted.scale != now.scale)
                fields.emplace_back("scale", std::format("{:g}", wanted.scale));
            if (wanted.transform != now.transform)
                fields.emplace_back("transform", std::to_string(wanted.transform));
            if (wanted.vrr != now.vrr)
                fields.emplace_back("vrr", std::to_string(wanted.vrr));
            return fields;
        }

        // Everything, for a call of its own.
        std::vector<configuration::LuaField> everything(const DisplaySettings& wanted) {
            if (!wanted.enabled)
                return {{"disabled", "true"}};
            std::vector<configuration::LuaField> fields{{"mode", modeValue(wanted.mode)}, {"position", positionValue(wanted)}, {"scale", std::format("{:g}", wanted.scale)}};
            if (wanted.transform != 0)
                fields.emplace_back("transform", std::to_string(wanted.transform));
            if (wanted.vrr != 0)
                fields.emplace_back("vrr", std::to_string(wanted.vrr));
            return fields;
        }

    }

    std::expected<std::vector<Display>, std::string> parseDisplays(std::string_view reply) {
        const auto document = nlohmann::json::parse(reply, nullptr, false);
        if (document.is_discarded() || !document.is_array())
            return std::unexpected{"the monitors reply is not a JSON list"};
        std::vector<Display> displays;
        for (const auto& object : document) {
            if (!object.is_object())
                continue;
            Display display{
                .name        = text(object, "name"),
                .description = text(object, "description"),
                .modes       = {},
                .settings =
                    DisplaySettings{
                        .mode      = Mode{whole(object, "width"), whole(object, "height"), std::round(number(object, "refreshRate") * 100) / 100},
                        .x         = whole(object, "x"),
                        .y         = whole(object, "y"),
                        .scale     = number(object, "scale", 1),
                        .transform = whole(object, "transform"),
                        .vrr       = flag(object, "vrr") ? 1 : 0,
                        .enabled   = !flag(object, "disabled"),
                    },
            };
            if (const auto modes = object.find("availableModes"); modes != object.end() && modes->is_array())
                for (const auto& written : *modes)
                    if (written.is_string())
                        if (const auto mode = modeOf(written.get<std::string>()); mode && std::ranges::find(display.modes, *mode) == display.modes.end())
                            display.modes.push_back(*mode);
            if (!display.settings.enabled)
                display.settings.mode = {};
            displays.push_back(std::move(display));
        }
        return displays;
    }

    std::string modeText(const Mode& mode) {
        auto refresh = std::format("{:.2f}", mode.refresh);
        while (refresh.ends_with('0'))
            refresh.pop_back();
        if (refresh.ends_with('.'))
            refresh.pop_back();
        return std::format("{}x{}@{}", mode.width, mode.height, refresh);
    }

    std::pair<int, int> layoutSize(const DisplaySettings& settings) {
        const auto scale  = settings.scale > 0 ? settings.scale : 1.0;
        const auto width  = static_cast<int>(std::lround(settings.mode.width / scale));
        const auto height = static_cast<int>(std::lround(settings.mode.height / scale));
        return settings.transform % 2 == 1 ? std::pair{height, width} : std::pair{width, height};
    }

    std::vector<double> scalesFor(const Mode& mode, double current) {
        std::vector<double> scales;
        for (const double scale : {1.0, 1.25, 1.5, 1.6, 1.75, 2.0, 2.5, 3.0}) {
            const auto whole = [scale](int pixels) {
                const auto divided = pixels / scale;
                return std::abs(divided - std::round(divided)) < 0.01;
            };
            if (mode.width == 0 || (whole(mode.width) && whole(mode.height)))
                scales.push_back(scale);
        }
        if (current > 0 && std::ranges::find(scales, current) == scales.end()) {
            scales.push_back(current);
            std::ranges::sort(scales);
        }
        return scales;
    }

    std::pair<int, int> positionBeside(const DisplaySettings& placed, const DisplaySettings& other, Beside side) {
        const auto [width, height]           = layoutSize(placed);
        const auto [otherWidth, otherHeight] = layoutSize(other);
        switch (side) {
            case Beside::left: return {other.x - width, other.y};
            case Beside::right: return {other.x + otherWidth, other.y};
            case Beside::above: return {other.x, other.y - height};
            case Beside::below: return {other.x, other.y + otherHeight};
        }
        return {other.x, other.y};
    }

    namespace {

        const std::vector<std::string_view> zeroScalingPath{"xwayland", "force_zero_scaling"};
        constexpr std::string_view          gtkScaleName = "GDK_SCALE";

        SharpApps sharpAppsIn(std::string_view config) {
            SharpApps sharp;
            if (const auto found = syntax::luaCallField(config, "hl.config", zeroScalingPath))
                sharp.on = config.substr(found->begin, found->end - found->begin) == "true";
            if (const auto scale = configuration::luaEnv(config, gtkScaleName)) {
                int number = 0;
                if (std::from_chars(scale->data(), scale->data() + scale->size(), number).ec == std::errc{})
                    sharp.gtkScale = number;
            }
            return sharp;
        }

    }

    std::optional<SharpApps> currentSharpApps(const configuration::MainConfig& config) {
        if (!config.lua())
            return std::nullopt;
        std::ifstream stream{config.file, std::ios::binary};
        if (!stream)
            return std::nullopt;
        std::stringstream text;
        text << stream.rdbuf();
        return sharpAppsIn(text.str());
    }

    SharpApps sharpAppsFor(bool on, const std::vector<DisplaySettings>& wanted) {
        if (!on)
            return {};
        double largest = 1;
        for (const auto& settings : wanted)
            if (settings.enabled)
                largest = std::max(largest, settings.scale);
        const auto whole = static_cast<int>(std::floor(largest + 0.001));
        return {.on = true, .gtkScale = whole >= 2 ? std::optional{whole} : std::nullopt};
    }

    std::expected<std::optional<configuration::FileChange>, std::string> planDisplays(const std::vector<Display>& displays, const std::vector<DisplaySettings>& wanted,
                                                                                     const std::optional<SharpApps>& sharp, const configuration::MainConfig& config) {
        if (!config.lua())
            return std::unexpected{"displays are set only in a Lua config (hyprland.lua) so far"};
        if (wanted.size() != displays.size())
            return std::unexpected{"the displays changed while they were being set"};
        std::string problem;
        auto        planned = configuration::planEdit(config.file, [&](std::string_view config) {
            std::string text{config};
            for (std::size_t index = 0; index < displays.size(); ++index) {
                const auto& display = displays[index];
                if (wanted[index] == display.settings)
                    continue;
                const auto matches = [&display](std::string_view output) { return names(output, display); };
                const bool has     = std::ranges::any_of(configuration::luaMonitorOutputs(text), matches);
                const auto output  = display.description.empty() ? display.name : "desc:" + display.description;
                auto       edited  = configuration::withLuaMonitor(text, matches, output, has ? differences(display.settings, wanted[index]) : everything(wanted[index]));
                if (!edited) {
                    problem = std::format("{} {}", display.name, edited.error());
                    return std::string{config};
                }
                text = std::move(*edited);
            }
            if (sharp) {
                const auto now = sharpAppsIn(text);
                if (now.on != sharp->on)
                    text = configuration::withLuaConfigValueSet(text, zeroScalingPath, sharp->on ? "true" : "false");
                // Off, GTK's apps are scaled by Hyprland again, so a scale of their own would
                // make them twice as large.
                const auto gtkScale = sharp->on ? sharp->gtkScale : now.gtkScale ? std::optional{1} : std::nullopt;
                if (gtkScale && gtkScale != now.gtkScale)
                    text = configuration::withLuaEnv(text, gtkScaleName, std::to_string(*gtkScale));
            }
            return text;
        });
        if (!problem.empty())
            return std::unexpected{problem};
        return planned;
    }

}
