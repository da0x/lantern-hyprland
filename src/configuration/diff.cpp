// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/diff.hpp"

#include <algorithm>

namespace configuration {

    namespace {

        std::vector<std::string_view> splitLines(std::string_view text) {
            std::vector<std::string_view> lines;
            while (!text.empty()) {
                const auto end = text.find('\n');
                lines.push_back(text.substr(0, end));
                text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
            }
            return lines;
        }

    }

    std::vector<DiffLine> diffLines(std::string_view before, std::string_view after) {
        const auto oldLines = splitLines(before);
        const auto newLines = splitLines(after);
        const auto rows     = oldLines.size();
        const auto columns  = newLines.size();

        // The longest common subsequence, by dynamic programming: config files are short.
        std::vector<std::vector<int>> longest(rows + 1, std::vector<int>(columns + 1, 0));
        for (std::size_t row = rows; row-- > 0;)
            for (std::size_t column = columns; column-- > 0;)
                longest[row][column] = oldLines[row] == newLines[column] ? longest[row + 1][column + 1] + 1 : std::max(longest[row + 1][column], longest[row][column + 1]);

        std::vector<DiffLine> lines;
        std::size_t           row = 0;
        std::size_t           column = 0;
        while (row < rows || column < columns) {
            if (row < rows && column < columns && oldLines[row] == newLines[column]) {
                lines.push_back({DiffLine::Kind::same, std::string{oldLines[row]}, static_cast<int>(row + 1)});
                ++row;
                ++column;
            } else if (row < rows && (column == columns || longest[row + 1][column] >= longest[row][column + 1])) {
                lines.push_back({DiffLine::Kind::removed, std::string{oldLines[row]}, static_cast<int>(row + 1)});
                ++row;
            } else {
                lines.push_back({DiffLine::Kind::added, std::string{newLines[column]}, static_cast<int>(column + 1)});
                ++column;
            }
        }
        return lines;
    }

    std::vector<DiffLine> changesOnly(const std::vector<DiffLine>& lines, int context) {
        std::vector<bool> keep(lines.size(), false);
        for (std::size_t index = 0; index < lines.size(); ++index) {
            if (lines[index].kind == DiffLine::Kind::same)
                continue;
            const auto first = index >= static_cast<std::size_t>(context) ? index - context : 0;
            const auto last  = std::min(lines.size() - 1, index + context);
            for (auto near = first; near <= last; ++near)
                keep[near] = true;
        }
        std::vector<DiffLine> kept;
        for (std::size_t index = 0; index < lines.size(); ++index) {
            if (!keep[index])
                continue;
            if (!kept.empty() && index > 0 && !keep[index - 1])
                kept.push_back({DiffLine::Kind::same, "", 0});
            kept.push_back(lines[index]);
        }
        return kept;
    }

}
