// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/autostart.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <vector>

namespace configuration {

    namespace {

        std::string_view trim(std::string_view text) {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
                text.remove_suffix(1);
            return text;
        }

        struct Line {
            std::size_t      begin = 0;
            // Just past its newline, or the end of the text.
            std::size_t      end   = 0;
            std::string_view text;
        };

        std::vector<Line> linesOf(std::string_view config) {
            std::vector<Line> lines;
            std::size_t       at = 0;
            while (at < config.size()) {
                const auto newline = config.find('\n', at);
                const auto stop    = newline == std::string_view::npos ? config.size() : newline;
                lines.push_back({at, newline == std::string_view::npos ? config.size() : newline + 1, config.substr(at, stop - at)});
                at = stop + 1;
            }
            return lines;
        }

        std::string luaString(std::string_view text) {
            std::string quoted = "\"";
            for (const auto character : text) {
                if (character == '"' || character == '\\')
                    quoted += '\\';
                quoted += character;
            }
            return quoted + '"';
        }

        std::string callOf(bool lua, std::string_view command) {
            return lua ? std::format("hl.exec_cmd({})", luaString(command)) : std::format("exec-once = {}", command);
        }

        // The command of a line that is exactly such a call, whatever it starts.
        std::optional<std::string> commandOf(std::string_view line, bool lua) {
            const auto inner = trim(line);
            if (!lua) {
                const auto equals = inner.find('=');
                if (equals == std::string_view::npos || trim(inner.substr(0, equals)) != "exec-once")
                    return std::nullopt;
                return std::string{trim(inner.substr(equals + 1))};
            }
            constexpr std::string_view open = "hl.exec_cmd(\"";
            if (!inner.starts_with(open) || !inner.ends_with("\")") || inner.size() < open.size() + 2)
                return std::nullopt;
            const auto  quoted = inner.substr(open.size(), inner.size() - open.size() - 2);
            std::string command;
            for (std::size_t at = 0; at < quoted.size(); ++at) {
                if (quoted[at] == '"')
                    return std::nullopt;
                if (quoted[at] == '\\') {
                    if (++at == quoted.size() || (quoted[at] != '"' && quoted[at] != '\\'))
                        return std::nullopt;
                }
                command += quoted[at];
            }
            return command;
        }

        // The line without its comment mark, if it is commented out.
        std::optional<std::string_view> uncommented(std::string_view line, bool lua) {
            const auto inner = trim(line);
            const auto mark  = lua ? std::string_view{"--"} : std::string_view{"#"};
            if (!inner.starts_with(mark))
                return std::nullopt;
            return trim(inner.substr(mark.size()));
        }

        bool isCommentedCall(std::string_view line, bool lua, std::string_view command) {
            const auto rest = uncommented(line, lua);
            return rest && commandOf(*rest, lua) == command;
        }

        std::string_view indentOf(std::string_view line) {
            return line.substr(0, std::min(line.size(), line.find_first_not_of(" \t")));
        }

        // The same call with any spacing around the = of hyprland.conf.
        bool isCall(std::string_view line, bool lua, std::string_view command) {
            return commandOf(line, lua) == command;
        }

    }

    bool startsAtLogin(std::string_view config, bool lua, std::string_view command) {
        return std::ranges::any_of(linesOf(config), [&](const Line& line) { return isCall(line.text, lua, command); });
    }

    std::string withAutostart(std::string_view config, bool lua, std::string_view command, std::span<const std::string_view> instead) {
        const auto  lines = linesOf(config);
        std::string text;
        text.reserve(config.size() + 64);
        const bool already = startsAtLogin(config, lua, command);

        // Where the new line goes: before the end of the first start block, in Lua.
        std::optional<std::size_t> blockEnd;
        std::string                indent = "    ";
        if (lua && !already) {
            bool inside = false;
            for (std::size_t index = 0; index < lines.size(); ++index) {
                const auto inner = trim(lines[index].text);
                if (!inside && inner.starts_with("hl.on(\"hyprland.start\"") && inner.ends_with("function ()")) {
                    inside = true;
                    if (index + 1 < lines.size() && trim(lines[index + 1].text) != "end)") {
                        const auto next = lines[index + 1].text;
                        indent          = std::string{next.substr(0, next.find_first_not_of(" \t"))};
                    }
                } else if (inside && inner == "end)" && lines[index].text.find_first_not_of(" \t") == 0) {
                    blockEnd = index;
                    break;
                }
            }
        }

        for (std::size_t index = 0; index < lines.size(); ++index) {
            if (blockEnd && index == *blockEnd)
                text += std::format("{}{}\n", indent, callOf(true, command));
            if (std::ranges::any_of(instead, [&](std::string_view other) { return isCall(lines[index].text, lua, other); }))
                continue;
            text += config.substr(lines[index].begin, lines[index].end - lines[index].begin);
        }

        if (!already && !blockEnd) {
            if (!text.empty() && !text.ends_with('\n'))
                text += '\n';
            if (lua)
                text += std::format("\nhl.on(\"hyprland.start\", function ()\n    {}\nend)\n", callOf(true, command));
            else
                text += std::format("\n{}\n", callOf(false, command));
        }
        return text;
    }

    std::string withoutAutostart(std::string_view config, bool lua, std::string_view command) {
        std::string text;
        for (const auto& line : linesOf(config))
            if (!isCall(line.text, lua, command) && !isCommentedCall(line.text, lua, command))
                text += config.substr(line.begin, line.end - line.begin);
        return text;
    }

    std::vector<StartedProgram> startedAtLogin(std::string_view config, bool lua) {
        std::vector<StartedProgram> started;
        bool                        inside = !lua;
        for (const auto& line : linesOf(config)) {
            const auto inner = trim(line.text);
            if (lua && !inside) {
                inside = inner.starts_with("hl.on(\"hyprland.start\"") && inner.ends_with("function ()");
                continue;
            }
            if (lua && inner == "end)" && line.text.find_first_not_of(" \t") == 0) {
                inside = false;
                continue;
            }
            if (auto command = commandOf(line.text, lua))
                started.push_back({std::move(*command), true});
            else if (const auto rest = uncommented(line.text, lua))
                if (auto commented = commandOf(*rest, lua))
                    started.push_back({std::move(*commented), false});
        }
        return started;
    }

    std::string withAutostartSwitched(std::string_view config, bool lua, std::string_view command, bool on) {
        std::string text;
        for (const auto& line : linesOf(config)) {
            const auto ending = line.end > line.begin + line.text.size() ? config.substr(line.begin + line.text.size(), line.end - line.begin - line.text.size()) : "";
            if (on && isCommentedCall(line.text, lua, command))
                text += std::format("{}{}{}", indentOf(line.text), *uncommented(line.text, lua), ending);
            else if (!on && isCall(line.text, lua, command))
                text += std::format("{}{} {}{}", indentOf(line.text), lua ? "--" : "#", trim(line.text), ending);
            else
                text += config.substr(line.begin, line.end - line.begin);
        }
        return text;
    }

}
