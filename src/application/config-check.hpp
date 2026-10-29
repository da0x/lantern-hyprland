// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace application {

    // After a change to the Hyprland config: waits for Hyprland to read it again, asks it
    // for errors with `hyprctl configerrors`, and undoes `record` if it reports any. `done`
    // gets a sentence saying so, or nothing when all is well.
    void checkHyprlandConfig(const EventLoop& loop, const std::filesystem::path& hyprctl, const std::filesystem::path& record,
                             std::function<void(std::optional<std::string>)> done);

}
