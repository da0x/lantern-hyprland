// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "syntax/highlight.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace application {

    // One of the config's files as it is on disk, colored for showing.
    struct SourceFile {
        std::filesystem::path     path;
        std::string               text;
        std::vector<syntax::Span> spans;

        // Why the file could not be read. Empty when it was.
        std::string               error;
    };

    [[nodiscard]] SourceFile readSourceFile(const std::filesystem::path& path);

}
