// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace configuration {

    // An hl.bind("KEYS", action) call on a line of its own at the top of a Lua config.
    struct LuaBindLine {
        std::string                keys;
        // The action as written, Lua source.
        std::string                action;
        // The command, when the action is hl.dsp.exec_cmd with a plain string.
        std::optional<std::string> command;
        // The table of options after the action, such as { locked = true }, or nothing.
        std::string                options;
    };

    // Every such line, in order. Binds made in a loop or over several lines are not among
    // them, and Lantern does not change them.
    [[nodiscard]] std::vector<LuaBindLine> luaBindLines(std::string_view config);

    // Whether two keys name the same combination, such as "SUPER + V" and "super+v".
    [[nodiscard]] bool sameKeys(std::string_view one, std::string_view other);

    // `config` with `keys` running `command`, with `options`, Lua source, when given: the
    // line that binds `keys` is replaced, or a line is added after the last bind line, or
    // at the end.
    [[nodiscard]] std::string withLuaExecBind(std::string_view config, std::string_view keys, std::string_view command, std::string_view options = {});

    // `config` without the line that binds `keys`.
    [[nodiscard]] std::string withoutLuaBind(std::string_view config, std::string_view keys);

}
