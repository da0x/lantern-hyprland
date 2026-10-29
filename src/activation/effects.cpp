// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation/effects.hpp"

#include "syntax/highlight.hpp"

#include <algorithm>
#include <format>
#include <span>

namespace activation {

    namespace {

        // The calls in each area. A reload takes every one of them back.
        constexpr std::array<std::string_view, 7> optionCalls{"hl.config", "hl.window_rule", "hl.layer_rule", "hl.workspace_rule", "hl.animation", "hl.curve", "hl.monitor"};
        constexpr std::array<std::string_view, 2> keybindingCalls{"hl.bind", "hl.define_submap"};

        bool among(std::string_view call, std::span<const std::string_view> calls) {
            return std::ranges::find(calls, call) != calls.end();
        }

    }

    std::string_view nameOf(Area area) {
        switch (area) {
            case Area::keybindings: return "keybindings";
            case Area::programs: return "programs";
            case Area::theme: return "theme";
            case Area::options: break;
        }
        return "options";
    }

    std::optional<Area> areaNamed(std::string_view name) {
        for (const auto area : areas)
            if (nameOf(area) == name)
                return area;
        return std::nullopt;
    }

    std::vector<const Effect*> Effects::in(Area area) const {
        std::vector<const Effect*> found;
        for (const auto& effect : effects)
            if (effect.area == area)
                found.push_back(&effect);
        return found;
    }

    Effects readEffects(std::string_view text) {
        Effects read;
        for (const auto& statement : syntax::luaStatements(text)) {
            const auto source = std::string{text.substr(statement.range.begin, statement.range.end - statement.range.begin)};
            if (statement.broken) {
                read.skipped.push_back({statement.line, "Lantern cannot make sense of it"});
            } else if (among(statement.call, optionCalls)) {
                read.effects.push_back({.area = Area::options, .source = source, .line = statement.line, .keys = {}, .command = {}});
            } else if (among(statement.call, keybindingCalls)) {
                read.effects.push_back({.area = Area::keybindings, .source = source, .line = statement.line, .keys = statement.firstString.value_or(""), .command = {}});
            } else if (statement.call == "hl.exec_cmd" && statement.firstString && !statement.firstString->empty()) {
                read.effects.push_back({.area = Area::programs, .source = source, .line = statement.line, .keys = {}, .command = *statement.firstString});
            } else if (statement.call == "hl.exec_cmd") {
                read.skipped.push_back({statement.line, "a program to start is a plain command line in quotes"});
            } else if (statement.call == "hl.env") {
                read.skipped.push_back({statement.line, "an environment variable stays for every program started after it, so it cannot be taken back"});
            } else if (statement.call.empty()) {
                read.skipped.push_back({statement.line, "only hl.* calls are effects"});
            } else {
                read.skipped.push_back({statement.line, std::format("{} is not something Lantern can take back", statement.call)});
            }
        }
        return read;
    }

    std::filesystem::path effectsFile(const std::filesystem::path& lanternFolder) {
        return lanternFolder / "effects.lua";
    }

    std::string_view effectsTemplate() {
        return R"(-- Effects Lantern applies to the running desktop when you press Activate,
-- and takes back when you press Deactivate. Nothing here is written to your config.
--
-- It is plain Hyprland Lua: copy anything you like into your own config to keep it.
-- Each kind applies only while you allow it on the Activate page. Remove the "-- "
-- before a line to use it.

-- Options and rules, taken back by reloading Hyprland:
-- hl.config({ decoration = { rounding = 16 } })
-- hl.window_rule({ name = "float-calculator", match = { class = "^(org.gnome.Calculator)$" }, float = true })

-- Keybindings, taken back the same way:
-- hl.bind("SUPER + SHIFT + K", hl.dsp.exec_cmd("kitty"))

-- Programs you installed, started by Lantern and stopped again:
-- hl.exec_cmd("waybar")
)";
    }

    std::string luaFor(const Effects& effects, Area area) {
        std::string lua;
        for (const auto* effect : effects.in(area))
            lua += effect->source + "\n";
        return lua;
    }

}
