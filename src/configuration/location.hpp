// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <string>

namespace configuration {

    // A line in one of the config's files.
    struct Location {
        std::filesystem::path file;
        int                   line = 0;

        // "hyprland.lua:42": the file's name, without its folder.
        [[nodiscard]] std::string brief() const;

        friend bool operator==(const Location&, const Location&) = default;
    };

}
