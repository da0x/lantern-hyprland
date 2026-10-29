// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "syntax/highlight.hpp"

#include <hyprtoolkit/palette/Palette.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace ui {

    // Every hyprtoolkit text is Pango markup. These build it.

    // Text made safe to show as markup.
    [[nodiscard]] std::string escapeMarkup(std::string_view text);

    // "#rrggbb", ignoring alpha.
    [[nodiscard]] std::string hex(const Hyprtoolkit::CHyprColor& color);

    // Whether the theme is light text on dark, which decides the code colors.
    [[nodiscard]] bool darkTheme(const Hyprtoolkit::CPalette& palette);

    // `text` wrapped in a span of `color`, escaped.
    [[nodiscard]] std::string colored(std::string_view text, const std::string& color);

    // Bytes [begin, end) of a Lua source as markup, each span in its color and tabs as
    // spaces. `span` is where to start looking in `spans`, and is moved past the spans used,
    // so lines can be marked up one after another in a single pass.
    [[nodiscard]] std::string codeMarkup(std::string_view text, std::size_t begin, std::size_t end, const std::vector<syntax::Span>& spans,
                                         std::vector<syntax::Span>::const_iterator& span, const Hyprtoolkit::CPalette& palette);

    // A whole Lua expression as colored markup in the monospace font.
    [[nodiscard]] std::string luaMarkup(std::string_view code, const Hyprtoolkit::CPalette& palette);

    // Keys written "SUPER + SHIFT + Q", drawn as key caps.
    [[nodiscard]] std::string keyCaps(std::string_view keys, const Hyprtoolkit::CPalette& palette);

}
