// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace syntax {

    // What a piece of source is, as far as coloring it goes.
    enum class Kind {
        comment,
        keyword,
        string,
        number,
        constant,
        function,
        field,
        operation,
        punctuation,
    };

    // A run of bytes of one kind: [begin, end).
    struct Span {
        std::size_t begin = 0;
        std::size_t end   = 0;
        Kind        kind  = Kind::comment;

        friend bool operator==(const Span&, const Span&) = default;
    };

    // The colored runs of a Lua source, in order and never overlapping. Bytes outside
    // every span are plain. A source with syntax errors is still colored as far as the
    // parser can make sense of it.
    [[nodiscard]] std::vector<Span> highlightLua(std::string_view source);

    // The bytes [begin, end) of the first function in a Lua source that starts on `line`,
    // counting from 1: `function () ... end`, or a whole `function name() ... end`. Lua
    // reports only the line a function starts on, and this finds the function itself.
    struct Range {
        std::size_t begin = 0;
        std::size_t end   = 0;

        friend bool operator==(const Range&, const Range&) = default;
    };
    [[nodiscard]] std::optional<Range> luaFunctionAt(std::string_view source, int line);

    // The bytes of the value at `path` in the table passed to the first `call` that sets
    // it: for hl.config and {"general", "col", "active_border"}, the value of
    // hl.config({ general = { col = { active_border = ... } } }). Each step but the last
    // must be a table written in place.
    [[nodiscard]] std::optional<Range> luaCallField(std::string_view source, std::string_view call, const std::vector<std::string_view>& path);

    // A statement at the top of a Lua source, outside any function or block.
    struct Statement {
        Range                      range;
        // Its first line, counting from 1.
        int                        line = 0;
        // The function it calls, as written ("hl.bind"), when the statement is a call.
        std::string                call;
        // Its first argument, when that is a string literal, without the quotes.
        std::optional<std::string> firstString;
        // The parser could not make sense of it.
        bool                       broken = false;
    };

    // The top-level statements of a Lua source, in order; comments are not statements.
    [[nodiscard]] std::vector<Statement> luaStatements(std::string_view source);

}
