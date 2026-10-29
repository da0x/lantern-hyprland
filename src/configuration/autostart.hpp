// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace configuration {

    // A program the config starts at login, and whether that line is switched on or
    // commented out.
    struct StartedProgram {
        std::string command;
        bool        on = true;

        bool        operator==(const StartedProgram&) const = default;
    };

    // Every program the config starts at login, in its order: in a Lua config the
    // hl.exec_cmd("command") lines inside hl.on("hyprland.start", function () ... end)
    // blocks, and those commented out with --; in hyprland.conf the exec-once lines, and
    // those commented out with #.
    [[nodiscard]] std::vector<StartedProgram> startedAtLogin(std::string_view config, bool lua);

    // `config` with the lines starting `command` at login switched on, or commented out.
    [[nodiscard]] std::string withAutostartSwitched(std::string_view config, bool lua, std::string_view command, bool on);

    // Whether the config starts `command` at login: a line that is exactly
    // hl.exec_cmd("command") in a Lua config, or exec-once = command in hyprland.conf.
    [[nodiscard]] bool startsAtLogin(std::string_view config, bool lua, std::string_view command);

    // `config` starting `command` at login and not starting any of `instead`, with every
    // other byte as it was. In a Lua config the line goes at the end of the first
    // hl.on("hyprland.start", function () ... end) block, indented like its first line, and
    // a block is added at the end when there is none; in hyprland.conf an exec-once line
    // is added at the end. Only lines that are exactly such a call are removed, so a
    // command started in any other way is left alone.
    [[nodiscard]] std::string withAutostart(std::string_view config, bool lua, std::string_view command, std::span<const std::string_view> instead);

    // `config` no longer starting `command` at login: only lines that are exactly such a
    // call, or such a call commented out, are taken out.
    [[nodiscard]] std::string withoutAutostart(std::string_view config, bool lua, std::string_view command);

}
