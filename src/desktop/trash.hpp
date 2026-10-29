// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/environment.hpp"

#include <chrono>
#include <expected>
#include <filesystem>
#include <string>

namespace desktop {

    // $XDG_DATA_HOME/Trash, or ~/.local/share/Trash: the trash file managers show.
    [[nodiscard]] std::filesystem::path trashFolder(const foundation::Environment& environment);

    // Moves `file` to the trash, as the freedesktop.org trash specification has it: into
    // its files folder, under a name not taken there, with a .trashinfo beside it in its
    // info folder that says where it was and when, so a file manager can put it back.
    // Refuses a file on another drive than the trash, which only a copy could move.
    // Returns where it went.
    [[nodiscard]] std::expected<std::filesystem::path, std::string> moveToTrash(const std::filesystem::path& file, const std::filesystem::path& trash,
                                                                                std::chrono::system_clock::time_point when = std::chrono::system_clock::now());

}
