// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace desktop {

    // hyprtoolkit keeps the clipboard to itself, so Lantern copies through wl-copy, from
    // wl-clipboard, when it is installed.

    // wl-copy in the folders of `path`, a PATH value.
    [[nodiscard]] std::optional<std::filesystem::path> clipboardProgram(std::string_view path);

    // Puts `text` on the clipboard by running `program` with it. wl-copy returns as soon as
    // it has taken the text, leaving a copy of itself to serve it.
    [[nodiscard]] std::expected<void, std::string> copyText(const std::filesystem::path& program, const std::string& text);

}
