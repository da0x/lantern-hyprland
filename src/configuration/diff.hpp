// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace configuration {

    // One line of a line-by-line comparison of two texts.
    struct DiffLine {
        enum class Kind {
            same,
            removed,
            added,
        };
        Kind        kind = Kind::same;
        std::string text;
        // Its line number in the old text (for same and removed) or the new one (for added), from 1.
        int         line = 0;
    };

    // The shortest list of removed and added lines that turns `before` into `after`.
    [[nodiscard]] std::vector<DiffLine> diffLines(std::string_view before, std::string_view after);

    // Only the changes and `context` unchanged lines around each; a gap between two groups
    // is a single same line with empty text and line 0.
    [[nodiscard]] std::vector<DiffLine> changesOnly(const std::vector<DiffLine>& lines, int context = 3);

}
