// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/source-file.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

namespace application {

    SourceFile readSourceFile(const std::filesystem::path& path) {
        SourceFile    file{.path = path, .text = {}, .spans = {}, .error = {}};
        std::ifstream stream{path, std::ios::binary};
        if (!stream) {
            file.error = std::strerror(errno);
            return file;
        }
        std::ostringstream text;
        text << stream.rdbuf();
        file.text  = std::move(text).str();
        file.spans = syntax::highlightLua(file.text);
        return file;
    }

}
