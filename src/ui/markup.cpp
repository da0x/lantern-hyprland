// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/markup.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace ui {

    namespace {

        constexpr int tabWidth = 4;

        // Each kind in a hue of its own, so that a keyword and a function name never look
        // alike whatever the theme's accent is.
        std::string colorOf(const Hyprtoolkit::CPalette& palette, syntax::Kind kind) {
            const bool dark = darkTheme(palette);
            switch (kind) {
                case syntax::Kind::comment: return dark ? "#7d8799" : "#6a737d";
                case syntax::Kind::keyword: return dark ? "#c792ea" : "#8839a8";
                case syntax::Kind::string: return dark ? "#a5d68a" : "#2f7d32";
                case syntax::Kind::number:
                case syntax::Kind::constant: return dark ? "#f2a272" : "#b35400";
                case syntax::Kind::function: return dark ? "#82aaff" : "#1d5fbf";
                case syntax::Kind::field: return dark ? "#7fd4e8" : "#0b7285";
                case syntax::Kind::operation:
                case syntax::Kind::punctuation: return dark ? "#9aa5b4" : "#57606a";
            }
            return hex(palette.m_colors.text);
        }

    }

    std::string escapeMarkup(std::string_view text) {
        std::string escaped;
        escaped.reserve(text.size());
        for (const char character : text) {
            switch (character) {
                case '&': escaped += "&amp;"; break;
                case '<': escaped += "&lt;"; break;
                case '>': escaped += "&gt;"; break;
                default: escaped += character;
            }
        }
        return escaped;
    }

    std::string hex(const Hyprtoolkit::CHyprColor& color) {
        const auto channel = [](double value) { return static_cast<int>(std::lround(std::clamp(value, 0.0, 1.0) * 255)); };
        return std::format("#{:02x}{:02x}{:02x}", channel(color.r), channel(color.g), channel(color.b));
    }

    bool darkTheme(const Hyprtoolkit::CPalette& palette) {
        return palette.m_colors.background.asOkLab().l < 0.5;
    }

    std::string colored(std::string_view text, const std::string& color) {
        return std::format("<span foreground=\"{}\">{}</span>", color, escapeMarkup(text));
    }

    std::string codeMarkup(std::string_view text, std::size_t begin, std::size_t end, const std::vector<syntax::Span>& spans, std::vector<syntax::Span>::const_iterator& span,
                           const Hyprtoolkit::CPalette& palette) {
        std::string markup;
        int         column = 0;
        std::size_t at     = begin;
        const auto  plain  = [&](std::size_t until) {
            for (; at < until; ++at) {
                const char character = text[at];
                if (character == '\t') {
                    const int spaces = tabWidth - (column % tabWidth);
                    markup.append(static_cast<std::size_t>(spaces), ' ');
                    column += spaces;
                    continue;
                }
                // A continuation byte of UTF-8 does not start a new column.
                if ((static_cast<unsigned char>(character) & 0xC0U) != 0x80U)
                    ++column;
                markup += escapeMarkup(std::string_view{&text[at], 1});
            }
        };

        while (at < end) {
            while (span != spans.end() && span->end <= at)
                ++span;
            if (span == spans.end() || span->begin >= end) {
                plain(end);
                break;
            }
            plain(std::max(at, span->begin));
            markup += std::format("<span foreground=\"{}\"{}>", colorOf(palette, span->kind), span->kind == syntax::Kind::comment ? " style=\"italic\"" : "");
            plain(std::min(span->end, end));
            markup += "</span>";
        }
        return markup;
    }

    std::string luaMarkup(std::string_view code, const Hyprtoolkit::CPalette& palette) {
        const auto spans = syntax::highlightLua(code);
        auto       span  = spans.cbegin();
        return std::format("<span font_family=\"{}\">{}</span>", escapeMarkup(palette.m_vars.fontFamilyMonospace), codeMarkup(code, 0, code.size(), spans, span, palette));
    }

    std::string keyCaps(std::string_view keys, const Hyprtoolkit::CPalette& palette) {
        const auto  cap  = hex(palette.m_colors.base.mix(palette.m_colors.text, 0.16F));
        const auto  join = hex(palette.m_colors.text.mix(palette.m_colors.base, 0.5F));
        // hyprgraphics adds an attribute of its own from the first character on, which
        // undoes a size set by a span that starts there, so the caps start one character in.
        std::string markup = "\u200b";
        bool        first  = true;
        while (!keys.empty()) {
            // "CTRL + +" ends in the + key, so a separator is " + " with something after it.
            auto split = keys.find(" + ");
            if (split == 0 || split == std::string_view::npos || split + 3 >= keys.size())
                split = std::string_view::npos;
            const auto key = keys.substr(0, split);
            if (!first)
                markup += std::format("<span foreground=\"{}\"> + </span>", join);
            first = false;
            markup += std::format("<span background=\"{}\" weight=\"600\" size=\"small\"> {} </span>", cap, escapeMarkup(key));
            keys = split == std::string_view::npos ? std::string_view{} : keys.substr(split + 3);
        }
        return markup;
    }

}
