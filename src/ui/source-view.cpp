// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/source-view.hpp"

#include "ui/markup.hpp"

#include <format>

namespace ui {

    namespace {

        constexpr double lineHeight  = 20;
        constexpr double numberWidth = 44;

    }

    SourceView::SourceView(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer) :
        palette{std::move(palette)},
        lines{this->palette, std::move(defer),
              {RowList::Column{.width = numberWidth, .monospace = true, .secondary = true, .markup = true, .heading = {}},
               RowList::Column{.width = 0, .monospace = true, .secondary = false, .markup = true, .heading = {}}},
              lineHeight} {}

    void SourceView::show(const application::SourceFile& file, int marked, bool jump) {
        std::vector<RowList::Row> rows;
        if (!file.error.empty()) {
            rows.push_back(RowList::Row{.cells = {{}, escapeMarkup(std::format("Could not read this file: {}.", file.error))}, .marked = false});
            lines.setRows(std::move(rows));
            return;
        }

        const std::string_view text   = file.text;
        const auto             accent = hex(palette->m_colors.accent);
        auto                   span   = file.spans.cbegin();
        std::size_t            begin  = 0;
        int                    number = 1;
        while (begin < text.size()) {
            auto end = text.find('\n', begin);
            if (end == std::string_view::npos)
                end = text.size();
            auto shownEnd = end;
            if (shownEnd > begin && text[shownEnd - 1] == '\r')
                --shownEnd;

            auto gutter = std::format("{:>4}", number);
            if (number == marked)
                gutter = std::format("<span foreground=\"{}\" weight=\"bold\">{}</span>", accent, gutter);
            rows.push_back(RowList::Row{.cells = {std::move(gutter), codeMarkup(text, begin, shownEnd, file.spans, span, *palette)}, .marked = number == marked});
            begin = end + 1;
            ++number;
        }

        lines.setRows(std::move(rows));
        if (jump)
            lines.scrollTo(marked > 0 ? static_cast<std::size_t>(marked - 1) : 0);
    }

}
