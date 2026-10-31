// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/lua-edit.hpp"

#include "syntax/highlight.hpp"

#include <algorithm>
#include <format>
#include <regex>

namespace configuration {

    std::expected<std::string, std::string> withLuaConfigValue(std::string_view config, const std::vector<std::string_view>& path, std::string_view value) {
        const auto found = syntax::luaCallField(config, "hl.config", path);
        if (!found) {
            std::string dotted;
            for (const auto part : path)
                dotted += (dotted.empty() ? "" : ".") + std::string{part};
            return std::unexpected{std::format("the config does not set {} in hl.config({{...}})", dotted)};
        }
        std::string text{config};
        text.replace(found->begin, found->end - found->begin, value);
        return text;
    }

    std::string withLuaConfigValueSet(std::string_view config, const std::vector<std::string_view>& path, std::string_view value) {
        if (auto edited = withLuaConfigValue(config, path, value))
            return std::move(*edited);
        // hl.config({ a = { b = value } }), nested a level per step of the path.
        std::string table{value};
        for (auto step = path.rbegin(); step != path.rend(); ++step)
            table = std::format("{{ {} = {} }}", *step, table);
        std::string text{config};
        if (!text.empty() && !text.ends_with('\n'))
            text += '\n';
        return text + std::format("\nhl.config({})\n", table);
    }

    namespace {

        // Each hl.env("name", "value") at the start of a line: where its value's quotes are.
        struct EnvCall {
            std::size_t valueBegin;
            std::size_t valueEnd;
        };

        std::optional<EnvCall> lastEnv(std::string_view config, std::string_view name) {
            const std::regex pattern{std::format(R"(^[ \t]*hl\.env\(\s*"{}"\s*,\s*"([^"\n]*)\")", std::string{name}), std::regex::multiline};
            std::optional<EnvCall> last;
            for (auto match = std::cregex_iterator{config.data(), config.data() + config.size(), pattern}; match != std::cregex_iterator{}; ++match)
                last = EnvCall{static_cast<std::size_t>(match->position(1)), static_cast<std::size_t>(match->position(1) + match->length(1))};
            return last;
        }

    }

    std::string withLuaEnv(std::string_view config, std::string_view name, std::string_view value) {
        std::string text{config};
        if (const auto found = lastEnv(config, name)) {
            text.replace(found->valueBegin, found->valueEnd - found->valueBegin, value);
            return text;
        }
        if (!text.empty() && !text.ends_with('\n'))
            text += '\n';
        return text + std::format("hl.env(\"{}\", \"{}\")\n", name, value);
    }

    std::optional<std::string> luaEnv(std::string_view config, std::string_view name) {
        const auto found = lastEnv(config, name);
        if (!found)
            return std::nullopt;
        return std::string{config.substr(found->valueBegin, found->valueEnd - found->valueBegin)};
    }

    namespace {

        constexpr std::string_view monitorCall = "hl.monitor";

        struct MonitorCall {
            syntax::Range range;
            std::string   output;
        };

        std::vector<MonitorCall> monitorCalls(std::string_view config) {
            std::vector<MonitorCall> calls;
            for (const auto& statement : syntax::luaStatements(config)) {
                if (statement.broken || statement.call != monitorCall)
                    continue;
                const auto text   = config.substr(statement.range.begin, statement.range.end - statement.range.begin);
                const auto output = syntax::luaCallField(text, monitorCall, {"output"});
                if (!output)
                    continue;
                const auto value = text.substr(output->begin, output->end - output->begin);
                if (value.size() < 2 || value.front() != '"' || value.back() != '"')
                    continue;
                calls.push_back(MonitorCall{statement.range, std::string{value.substr(1, value.size() - 2)}});
            }
            return calls;
        }

        // A string, number or boolean written as is, which Lantern may replace.
        bool literal(std::string_view value) {
            if (value == "true" || value == "false")
                return true;
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                return !value.substr(1, value.size() - 2).contains('"');
            return !value.empty() && std::ranges::all_of(value, [](char c) { return (c >= '0' && c <= '9') || c == '.' || c == '-'; });
        }

        // The call at `range` with `field` set: its value replaced, or a line of its own
        // added before the table's closing brace, lined up with the output's.
        std::expected<std::string, std::string> withField(std::string_view config, syntax::Range range, const LuaField& field) {
            const auto call  = config.substr(range.begin, range.end - range.begin);
            std::string text{config};
            if (const auto found = syntax::luaCallField(call, monitorCall, {field.first})) {
                const auto value = call.substr(found->begin, found->end - found->begin);
                if (!literal(value))
                    return std::unexpected{std::format("its hl.monitor call computes {}, which Lantern does not rewrite", field.first)};
                text.replace(range.begin + found->begin, found->end - found->begin, field.second);
                return text;
            }
            const auto close = call.rfind('}');
            const auto last  = call.find_last_not_of(" \t\r\n", close == std::string_view::npos || close == 0 ? std::string_view::npos : close - 1);
            if (close == std::string_view::npos || last == std::string_view::npos)
                return std::unexpected{"its hl.monitor call is not a table Lantern can add to"};
            const bool separated = call[last] == ',' || call[last] == '{';
            const auto lineEnd   = call.rfind('\n', close);
            if (lineEnd == std::string_view::npos || lineEnd < last) {
                // All on one line: hl.monitor({ output = "", mode = "preferred" }).
                text.insert(range.begin + last + 1, std::format("{}{} = {}{}", separated ? " " : ", ", field.first, field.second, call[last] == ',' ? "," : ""));
                return text;
            }
            // A line like the output's: its indent, and the same width up to its "=".
            std::string indent = "    ";
            std::size_t width  = field.first.size() + 1;
            if (const auto output = syntax::luaCallField(call, monitorCall, {"output"})) {
                const auto lineStart = call.rfind('\n', output->begin) + 1;
                const auto before    = call.substr(lineStart, output->begin - lineStart);
                const auto key       = before.find_first_not_of(" \t");
                const auto equals    = before.find('=');
                if (key != std::string_view::npos && equals != std::string_view::npos && equals > key) {
                    indent = std::string{before.substr(0, key)};
                    width  = std::max(width, equals - key);
                }
            }
            text.insert(range.begin + lineEnd + 1, std::format("{}{:<{}}= {},\n", indent, field.first, width, field.second));
            if (!separated)
                text.insert(range.begin + last + 1, ",");
            return text;
        }

    }

    std::vector<std::string> luaMonitorOutputs(std::string_view config) {
        std::vector<std::string> outputs;
        for (auto& call : monitorCalls(config))
            outputs.push_back(std::move(call.output));
        return outputs;
    }

    std::expected<std::string, std::string> withLuaMonitor(std::string_view config, const std::function<bool(std::string_view)>& matches, std::string_view output,
                                                           const std::vector<LuaField>& fields) {
        std::string text{config};
        const auto  matching = [&matches](std::string_view now) {
            std::optional<syntax::Range> found;
            for (const auto& call : monitorCalls(now))
                if (matches(call.output))
                    found = call.range;
            return found;
        };
        if (matching(text)) {
            // A field at a time, finding the call again, since each one moves what follows.
            for (const auto& field : fields) {
                auto edited = withField(text, *matching(text), field);
                if (!edited)
                    return edited;
                text = std::move(*edited);
            }
            return text;
        }
        std::size_t width = std::string_view{"output"}.size();
        for (const auto& field : fields)
            width = std::max(width, field.first.size());
        auto made = std::format("hl.monitor({{\n    {:<{}} = \"{}\",\n", "output", width, output);
        for (const auto& [name, value] : fields)
            made += std::format("    {:<{}} = {},\n", name, width, value);
        made += "})";
        const auto calls = monitorCalls(text);
        if (calls.empty()) {
            if (!text.empty() && !text.ends_with('\n'))
                text += '\n';
            return text + "\n" + made + "\n";
        }
        text.insert(calls.back().range.end, "\n\n" + made);
        return text;
    }

    namespace {

        // Where the window rule by `name` is, from the start of its line to just past its
        // closing line.
        std::optional<std::pair<std::size_t, std::size_t>> windowRuleIn(std::string_view config, std::string_view name) {
            const auto named = config.find(std::format("\"{}\"", name));
            if (named == std::string_view::npos)
                return std::nullopt;
            const auto begin = config.rfind("hl.window_rule({", named);
            const auto close = config.find("\n})", named);
            if (begin == std::string_view::npos || close == std::string_view::npos || (begin > 0 && config[begin - 1] != '\n'))
                return std::nullopt;
            const auto end = config.find('\n', close + 1);
            return std::pair{begin, end == std::string_view::npos ? config.size() : end + 1};
        }

    }

    bool hasLuaWindowRule(std::string_view config, std::string_view name) {
        return windowRuleIn(config, name).has_value();
    }

    std::string withLuaWindowRule(std::string_view config, std::string_view name, std::string_view rule, bool on) {
        std::string text{config};
        const auto  found = windowRuleIn(text, name);
        if (!on && found) {
            auto begin = found->first;
            // With the blank line before it, which it was added with.
            if (begin >= 2 && text[begin - 1] == '\n' && text[begin - 2] == '\n')
                --begin;
            text.erase(begin, found->second - begin);
        } else if (on && !found) {
            // After the last window rule, or at the end.
            auto last = text.rfind("\nhl.window_rule(");
            if (last == std::string::npos && text.starts_with("hl.window_rule("))
                last = 0;
            const auto close = last == std::string::npos ? std::string::npos : text.find("\n})", last + 1);
            if (close == std::string::npos) {
                if (!text.empty() && !text.ends_with('\n'))
                    text += '\n';
                text += std::format("\n{}", rule);
            } else {
                const auto end = text.find('\n', close + 1);
                const auto at  = end == std::string::npos ? text.size() : end + 1;
                text.insert(at, std::format("\n{}", rule));
            }
        }
        return text;
    }

    namespace {

        // Where the block by `name` is: from the start of its first line to just past its
        // last, and where the text between them starts and ends.
        struct BlockPlace {
            std::size_t begin;
            std::size_t inside;
            std::size_t insideEnd;
            std::size_t end;
        };

        std::optional<BlockPlace> blockIn(std::string_view config, std::string_view name) {
            const auto opening = std::format("-- Lantern: {}\n", name);
            const auto closing = std::format("-- Lantern: end of {}", name);
            auto       begin   = config.find(opening);
            while (begin != std::string_view::npos && begin > 0 && config[begin - 1] != '\n')
                begin = config.find(opening, begin + 1);
            if (begin == std::string_view::npos)
                return std::nullopt;
            const auto inside = begin + opening.size();
            auto       close  = config.find(closing, inside);
            while (close != std::string_view::npos && config[close - 1] != '\n')
                close = config.find(closing, close + 1);
            if (close == std::string_view::npos)
                return std::nullopt;
            const auto after = close + closing.size();
            if (after < config.size() && config[after] != '\n')
                return std::nullopt;
            return BlockPlace{begin, inside, close, after < config.size() ? after + 1 : after};
        }

    }

    std::optional<std::string_view> luaBlock(std::string_view config, std::string_view name) {
        const auto found = blockIn(config, name);
        if (!found)
            return std::nullopt;
        return config.substr(found->inside, found->insideEnd - found->inside);
    }

    std::string withLuaBlock(std::string_view config, std::string_view name, std::string_view text, bool on) {
        std::string body{text};
        if (!body.empty() && !body.ends_with('\n'))
            body += '\n';
        const auto whole = std::format("-- Lantern: {}\n{}-- Lantern: end of {}\n", name, body, name);
        std::string result{config};
        const auto  found = blockIn(result, name);
        if (found && on) {
            result.replace(found->begin, found->end - found->begin, whole);
        } else if (found) {
            auto begin = found->begin;
            // With the blank line before it, which it was added with.
            if (begin >= 2 && result[begin - 1] == '\n' && result[begin - 2] == '\n')
                --begin;
            result.erase(begin, found->end - begin);
        } else if (on) {
            if (!result.empty() && !result.ends_with('\n'))
                result += '\n';
            if (!result.empty())
                result += '\n';
            result += whole;
        }
        return result;
    }

}
