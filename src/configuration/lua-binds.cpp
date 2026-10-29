// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/lua-binds.hpp"

#include "configuration/bindings.hpp"

#include <algorithm>
#include <format>

namespace configuration {

    namespace {

        std::string_view trim(std::string_view text) {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
                text.remove_suffix(1);
            return text;
        }

        // A Lua string literal at the start of `text`: its value and its length in `text`.
        std::optional<std::pair<std::string, std::size_t>> luaString(std::string_view text) {
            if (text.starts_with("[[")) {
                const auto close = text.find("]]", 2);
                if (close == std::string_view::npos)
                    return std::nullopt;
                return std::pair{std::string{text.substr(2, close - 2)}, close + 2};
            }
            if (text.empty() || (text.front() != '"' && text.front() != '\''))
                return std::nullopt;
            const auto  quote = text.front();
            std::string value;
            for (std::size_t at = 1; at < text.size(); ++at) {
                if (text[at] == quote)
                    return std::pair{value, at + 1};
                if (text[at] == '\\') {
                    if (++at == text.size() || (text[at] != '"' && text[at] != '\'' && text[at] != '\\'))
                        return std::nullopt;
                }
                value += text[at];
            }
            return std::nullopt;
        }

        std::string luaLiteral(std::string_view value) {
            if (!value.contains('"') && !value.contains('\\'))
                return std::format("\"{}\"", value);
            if (!value.contains("]]") && !value.ends_with(']'))
                return std::format("[[{}]]", value);
            std::string quoted = "\"";
            for (const auto character : value) {
                if (character == '"' || character == '\\')
                    quoted += '\\';
                quoted += character;
            }
            return quoted + '"';
        }

        struct Line {
            std::size_t begin = 0;
            // Just past its newline, or the end of the text.
            std::size_t end   = 0;
            std::optional<LuaBindLine> bind;
        };

        std::optional<LuaBindLine> bindOf(std::string_view line) {
            if (!line.starts_with("hl.bind("))
                return std::nullopt;
            const auto inner = trim(line);
            if (!inner.ends_with(')'))
                return std::nullopt;
            const auto keys = luaString(inner.substr(8));
            if (!keys)
                return std::nullopt;
            auto rest = trim(inner.substr(8 + keys->second));
            if (!rest.starts_with(','))
                return std::nullopt;
            rest = trim(rest.substr(1, rest.size() - 2));
            // The action ends at the first comma outside brackets and strings, where the
            // options begin.
            std::size_t depth = 0;
            std::size_t end   = 0;
            while (end < rest.size()) {
                const auto character = rest[end];
                if (character == '"' || character == '\'' || rest.substr(end).starts_with("[[")) {
                    const auto literal = luaString(rest.substr(end));
                    if (!literal)
                        return std::nullopt;
                    end += literal->second;
                    continue;
                }
                if (character == '(' || character == '{' || character == '[')
                    ++depth;
                else if ((character == ')' || character == '}' || character == ']') && depth > 0)
                    --depth;
                else if (character == ',' && depth == 0)
                    break;
                ++end;
            }
            LuaBindLine bind{.keys = keys->first, .action = std::string{trim(rest.substr(0, end))}, .command = std::nullopt,
                             .options = end < rest.size() ? std::string{trim(rest.substr(end + 1))} : std::string{}};
            rest = trim(rest.substr(0, end));
            constexpr std::string_view exec = "hl.dsp.exec_cmd(";
            if (rest.starts_with(exec) && rest.ends_with(')'))
                if (const auto command = luaString(rest.substr(exec.size())); command && exec.size() + command->second == rest.size() - 1)
                    bind.command = command->first;
            return bind;
        }

        std::vector<Line> linesOf(std::string_view config) {
            std::vector<Line> lines;
            std::size_t       at = 0;
            while (at < config.size()) {
                const auto newline = config.find('\n', at);
                const auto end     = newline == std::string_view::npos ? config.size() : newline + 1;
                lines.push_back({at, end, bindOf(config.substr(at, (newline == std::string_view::npos ? config.size() : newline) - at))});
                at = end;
            }
            return lines;
        }

    }

    std::vector<LuaBindLine> luaBindLines(std::string_view config) {
        std::vector<LuaBindLine> binds;
        for (auto& line : linesOf(config))
            if (line.bind)
                binds.push_back(std::move(*line.bind));
        return binds;
    }

    bool sameKeys(std::string_view one, std::string_view other) {
        const auto first  = parseKeys(one);
        const auto second = parseKeys(other);
        if (!first || !second)
            return one == other;
        return first->first == second->first &&
            std::ranges::equal(first->second, second->second, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    }

    std::string withLuaExecBind(std::string_view config, std::string_view keys, std::string_view command, std::string_view options) {
        const auto lines   = linesOf(config);
        const auto written = options.empty() ? std::format("hl.bind({}, hl.dsp.exec_cmd({}))\n", luaLiteral(keys), luaLiteral(command))
                                             : std::format("hl.bind({}, hl.dsp.exec_cmd({}), {})\n", luaLiteral(keys), luaLiteral(command), options);
        const auto same    = std::ranges::find_if(lines, [&](const Line& line) { return line.bind && sameKeys(line.bind->keys, keys); });
        std::string text{config};
        if (same != lines.end())
            return text.replace(same->begin, same->end - same->begin, written);
        const auto last = std::ranges::find_if(lines.rbegin(), lines.rend(), [](const Line& line) { return line.bind.has_value(); });
        if (last != lines.rend()) {
            if (!text.substr(0, last->end).ends_with('\n'))
                text.insert(last->end, "\n");
            return text.insert(last->end + (config.substr(0, last->end).ends_with('\n') ? 0 : 1), written);
        }
        if (!text.empty() && !text.ends_with('\n'))
            text += '\n';
        return text + "\n" + written;
    }

    std::string withoutLuaBind(std::string_view config, std::string_view keys) {
        std::string text;
        for (const auto& line : linesOf(config))
            if (!line.bind || !sameKeys(line.bind->keys, keys))
                text += config.substr(line.begin, line.end - line.begin);
        return text;
    }

}
