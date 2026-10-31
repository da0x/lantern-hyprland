// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace configuration {

    // A Lua config with the value at `path` in hl.config({...}) replaced by `value`, Lua
    // source, and every other byte as it was. Refuses when the config does not write that
    // value in place, as a literal in an hl.config table: Lantern does not rewrite code
    // that computes it.
    [[nodiscard]] std::expected<std::string, std::string> withLuaConfigValue(std::string_view config, const std::vector<std::string_view>& path, std::string_view value);

    // The same, but where the config does not write the value in place, a call of its own
    // that sets it is added at the end, where it is read last and so wins.
    [[nodiscard]] std::string withLuaConfigValueSet(std::string_view config, const std::vector<std::string_view>& path, std::string_view value);

    // A Lua config whose hl.env("name", "...") call sets `value`, a plain string: the last
    // such call is changed in place, or one is added at the end. Calls in comments are left.
    [[nodiscard]] std::string withLuaEnv(std::string_view config, std::string_view name, std::string_view value);

    // The value the config's last hl.env("name", "...") call sets, if any.
    [[nodiscard]] std::optional<std::string> luaEnv(std::string_view config, std::string_view name);

    // A field of an hl.monitor({...}) table and its value, Lua source: {"mode", "\"1920x1080@60\""}.
    using LuaField = std::pair<std::string, std::string>;

    // The output of each hl.monitor({...}) call at the top of a Lua config, in order, when it
    // is written as a string.
    [[nodiscard]] std::vector<std::string> luaMonitorOutputs(std::string_view config);

    // A Lua config whose last hl.monitor({...}) call at the top, of those whose output
    // `matches`, has `fields`: each changed in place, or added to its table. When no call
    // matches, a call of its own for `output` with the fields goes after the last
    // hl.monitor call, or at the end. Refuses to change a value that is not a literal.
    [[nodiscard]] std::expected<std::string, std::string> withLuaMonitor(std::string_view config, const std::function<bool(std::string_view)>& matches, std::string_view output,
                                                                         const std::vector<LuaField>& fields);

    // Whether the config has the window rule named `name`, an hl.window_rule call starting a
    // line of its own.
    [[nodiscard]] bool hasLuaWindowRule(std::string_view config, std::string_view name);

    // The config with `rule`, a whole hl.window_rule call named `name`, after its last window
    // rule or at the end, when `on`; without it, and the blank line before it, when not.
    [[nodiscard]] std::string withLuaWindowRule(std::string_view config, std::string_view name, std::string_view rule, bool on);

    // The lines between "-- Lantern: <name>" and "-- Lantern: end of <name>", each a line of
    // its own, when the config has both.
    [[nodiscard]] std::optional<std::string_view> luaBlock(std::string_view config, std::string_view name);

    // The config with `text`, whole lines, between those two lines when `on`: in place of
    // what was there, or at the end. Without them, and the blank line before, when not.
    [[nodiscard]] std::string withLuaBlock(std::string_view config, std::string_view name, std::string_view text, bool on);

}
