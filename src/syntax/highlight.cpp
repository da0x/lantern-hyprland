// SPDX-License-Identifier: GPL-3.0-or-later
#include "syntax/highlight.hpp"

#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-lua.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace syntax {

    namespace {

        constexpr char luaQuery[] = {
#embed "lua-highlights.scm"
        };

        struct DeleteParser {
            void operator()(TSParser* parser) const { ts_parser_delete(parser); }
        };
        struct DeleteTree {
            void operator()(TSTree* tree) const { ts_tree_delete(tree); }
        };
        struct DeleteQuery {
            void operator()(TSQuery* query) const { ts_query_delete(query); }
        };
        struct DeleteCursor {
            void operator()(TSQueryCursor* cursor) const { ts_query_cursor_delete(cursor); }
        };

        std::optional<Kind> kindNamed(std::string_view name) {
            constexpr std::array kinds{
                std::pair{std::string_view{"comment"}, Kind::comment},   std::pair{std::string_view{"keyword"}, Kind::keyword},
                std::pair{std::string_view{"string"}, Kind::string},     std::pair{std::string_view{"number"}, Kind::number},
                std::pair{std::string_view{"constant"}, Kind::constant}, std::pair{std::string_view{"function"}, Kind::function},
                std::pair{std::string_view{"field"}, Kind::field},       std::pair{std::string_view{"operation"}, Kind::operation},
                std::pair{std::string_view{"punctuation"}, Kind::punctuation},
            };
            const auto found = std::ranges::find(kinds, name, &std::pair<std::string_view, Kind>::first);
            return found != kinds.end() ? std::optional{found->second} : std::nullopt;
        }

        // Compiled once; the query is part of the program, so a failure is a bug the tests catch.
        const TSQuery* query() {
            static const std::unique_ptr<TSQuery, DeleteQuery> compiled = [] {
                std::uint32_t errorOffset = 0;
                TSQueryError  error       = TSQueryErrorNone;
                return std::unique_ptr<TSQuery, DeleteQuery>{ts_query_new(tree_sitter_lua(), luaQuery, sizeof(luaQuery), &errorOffset, &error)};
            }();
            return compiled.get();
        }

        struct Capture {
            std::size_t   begin;
            std::size_t   end;
            std::uint32_t pattern;
            Kind          kind;
        };

    }

    std::optional<Range> luaFunctionAt(std::string_view source, int line) {
        if (line < 1 || source.size() > UINT32_MAX)
            return std::nullopt;

        const std::unique_ptr<TSParser, DeleteParser> parser{ts_parser_new()};
        ts_parser_set_language(parser.get(), tree_sitter_lua());
        const std::unique_ptr<TSTree, DeleteTree> tree{ts_parser_parse_string(parser.get(), nullptr, source.data(), static_cast<std::uint32_t>(source.size()))};
        if (!tree)
            return std::nullopt;

        // Depth first, in source order, skipping whatever ends before the line.
        const auto  row = static_cast<std::uint32_t>(line - 1);
        TSTreeCursor cursor = ts_tree_cursor_new(ts_tree_root_node(tree.get()));
        std::optional<Range> found;
        while (!found) {
            const TSNode     node = ts_tree_cursor_current_node(&cursor);
            const std::string_view type{ts_node_type(node)};
            const bool       onLine = ts_node_start_point(node).row == row;
            if (onLine && (type == "function_definition" || type == "function_declaration")) {
                found = Range{.begin = ts_node_start_byte(node), .end = ts_node_end_byte(node)};
                break;
            }
            if (ts_node_end_point(node).row >= row && ts_node_start_point(node).row <= row && ts_tree_cursor_goto_first_child(&cursor))
                continue;
            bool moved = false;
            while (!moved) {
                if (ts_tree_cursor_goto_next_sibling(&cursor)) {
                    moved = true;
                } else if (!ts_tree_cursor_goto_parent(&cursor)) {
                    break;
                }
            }
            if (!moved)
                break;
        }
        ts_tree_cursor_delete(&cursor);
        return found;
    }

    std::optional<Range> luaCallField(std::string_view source, std::string_view call, const std::vector<std::string_view>& path) {
        if (path.empty() || source.size() > UINT32_MAX)
            return std::nullopt;
        const std::unique_ptr<TSParser, DeleteParser> parser{ts_parser_new()};
        ts_parser_set_language(parser.get(), tree_sitter_lua());
        const std::unique_ptr<TSTree, DeleteTree> tree{ts_parser_parse_string(parser.get(), nullptr, source.data(), static_cast<std::uint32_t>(source.size()))};
        if (!tree)
            return std::nullopt;

        const auto text = [source](TSNode node) { return source.substr(ts_node_start_byte(node), ts_node_end_byte(node) - ts_node_start_byte(node)); };
        // The value of the field `name` in a table constructor.
        const auto field = [&text](TSNode table, std::string_view name) -> std::optional<TSNode> {
            for (std::uint32_t index = 0; index < ts_node_named_child_count(table); ++index) {
                const auto child = ts_node_named_child(table, index);
                if (std::string_view{ts_node_type(child)} != "field")
                    continue;
                const auto key = ts_node_child_by_field_name(child, "name", 4);
                if (!ts_node_is_null(key) && text(key) == name) {
                    const auto value = ts_node_child_by_field_name(child, "value", 5);
                    if (!ts_node_is_null(value))
                        return value;
                }
            }
            return std::nullopt;
        };

        // Every call to `call`, in source order; the first whose table has the path wins.
        std::vector<TSNode> pending{ts_tree_root_node(tree.get())};
        std::vector<TSNode> calls;
        while (!pending.empty()) {
            const auto node = pending.back();
            pending.pop_back();
            if (std::string_view{ts_node_type(node)} == "function_call") {
                const auto name = ts_node_child_by_field_name(node, "name", 4);
                if (!ts_node_is_null(name) && text(name) == call)
                    calls.push_back(node);
            }
            for (std::uint32_t index = ts_node_named_child_count(node); index-- > 0;)
                pending.push_back(ts_node_named_child(node, index));
        }
        for (const auto& found : calls) {
            const auto arguments = ts_node_child_by_field_name(found, "arguments", 9);
            if (ts_node_is_null(arguments) || ts_node_named_child_count(arguments) == 0)
                continue;
            auto current = ts_node_named_child(arguments, 0);
            bool reached = true;
            for (std::size_t step = 0; step < path.size(); ++step) {
                if (std::string_view{ts_node_type(current)} != "table_constructor") {
                    reached = false;
                    break;
                }
                const auto value = field(current, path[step]);
                if (!value) {
                    reached = false;
                    break;
                }
                current = *value;
            }
            if (reached)
                return Range{.begin = ts_node_start_byte(current), .end = ts_node_end_byte(current)};
        }
        return std::nullopt;
    }

    std::vector<Statement> luaStatements(std::string_view source) {
        if (source.size() > UINT32_MAX)
            return {};
        const std::unique_ptr<TSParser, DeleteParser> parser{ts_parser_new()};
        ts_parser_set_language(parser.get(), tree_sitter_lua());
        const std::unique_ptr<TSTree, DeleteTree> tree{ts_parser_parse_string(parser.get(), nullptr, source.data(), static_cast<std::uint32_t>(source.size()))};
        if (!tree)
            return {};
        const auto text = [source](TSNode node) { return std::string{source.substr(ts_node_start_byte(node), ts_node_end_byte(node) - ts_node_start_byte(node))}; };

        std::vector<Statement> statements;
        const auto             root = ts_tree_root_node(tree.get());
        for (std::uint32_t index = 0; index < ts_node_named_child_count(root); ++index) {
            const auto             node = ts_node_named_child(root, index);
            const std::string_view type{ts_node_type(node)};
            if (type == "comment")
                continue;
            Statement statement{.range  = Range{.begin = ts_node_start_byte(node), .end = ts_node_end_byte(node)},
                                .line        = static_cast<int>(ts_node_start_point(node).row) + 1,
                                .call        = {},
                                .firstString = std::nullopt,
                                .broken      = type == "ERROR" || ts_node_has_error(node)};
            if (type == "function_call") {
                if (const auto name = ts_node_child_by_field_name(node, "name", 4); !ts_node_is_null(name))
                    statement.call = text(name);
                if (const auto arguments = ts_node_child_by_field_name(node, "arguments", 9); !ts_node_is_null(arguments) && ts_node_named_child_count(arguments) > 0) {
                    const auto first = ts_node_named_child(arguments, 0);
                    if (std::string_view{ts_node_type(first)} == "string")
                        if (const auto content = ts_node_child_by_field_name(first, "content", 7); !ts_node_is_null(content))
                            statement.firstString = text(content);
                    if (std::string_view{ts_node_type(first)} == "string" && !statement.firstString)
                        statement.firstString = std::string{};
                }
            }
            statements.push_back(std::move(statement));
        }
        return statements;
    }

    std::vector<Span> highlightLua(std::string_view source) {
        const auto* compiled = query();
        if (compiled == nullptr || source.size() > UINT32_MAX)
            return {};

        const std::unique_ptr<TSParser, DeleteParser> parser{ts_parser_new()};
        ts_parser_set_language(parser.get(), tree_sitter_lua());
        const std::unique_ptr<TSTree, DeleteTree> tree{ts_parser_parse_string(parser.get(), nullptr, source.data(), static_cast<std::uint32_t>(source.size()))};
        if (!tree)
            return {};

        const std::unique_ptr<TSQueryCursor, DeleteCursor> cursor{ts_query_cursor_new()};
        ts_query_cursor_exec(cursor.get(), compiled, ts_tree_root_node(tree.get()));

        std::vector<Capture> captures;
        TSQueryMatch         match;
        while (ts_query_cursor_next_match(cursor.get(), &match)) {
            for (std::uint16_t index = 0; index < match.capture_count; ++index) {
                const auto&    capture = match.captures[index]; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): tree-sitter's array
                std::uint32_t  length  = 0;
                const char*    name    = ts_query_capture_name_for_id(compiled, capture.index, &length);
                const auto     kind    = kindNamed(std::string_view{name, length});
                const auto     begin   = ts_node_start_byte(capture.node);
                const auto     end     = ts_node_end_byte(capture.node);
                if (kind && end > begin)
                    captures.push_back(Capture{.begin = begin, .end = end, .pattern = match.pattern_index, .kind = *kind});
            }
        }

        // Paint the widest captures first so inner ones paint over them, and of two over the
        // same text, the earlier pattern last so that it wins.
        std::ranges::sort(captures, [](const Capture& left, const Capture& right) {
            const auto leftSize  = left.end - left.begin;
            const auto rightSize = right.end - right.begin;
            return leftSize != rightSize ? leftSize > rightSize : left.pattern > right.pattern;
        });
        std::vector<std::optional<Kind>> painted(source.size());
        for (const auto& capture : captures)
            std::ranges::fill(painted.begin() + static_cast<std::ptrdiff_t>(capture.begin), painted.begin() + static_cast<std::ptrdiff_t>(capture.end), capture.kind);

        std::vector<Span> spans;
        for (std::size_t at = 0; at < painted.size(); ++at) {
            if (!painted[at])
                continue;
            if (!spans.empty() && spans.back().end == at && spans.back().kind == *painted[at])
                spans.back().end = at + 1;
            else
                spans.push_back(Span{.begin = at, .end = at + 1, .kind = *painted[at]});
        }
        return spans;
    }

}
