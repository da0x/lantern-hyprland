// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"

#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace application {

    // Runs `program` with `arguments` and returns at once; `done` runs on the event loop
    // when it exits, with whether it succeeded and what it printed. For short commands
    // whose output fits in a pipe, such as hyprctl.
    [[nodiscard]] std::expected<void, std::string> runCommand(const EventLoop& loop, const std::filesystem::path& program, const std::vector<std::string>& arguments,
                                                              std::function<void(bool succeeded, std::string output)> done);

}
