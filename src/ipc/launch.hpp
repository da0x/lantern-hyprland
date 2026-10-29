// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "configuration/main-config.hpp"
#include "ipc/instance.hpp"

#include <expected>
#include <string>

namespace ipc {

    // The process ID of the running Hyprland, from the first line of the hyprland.lock
    // file in its runtime folder.
    [[nodiscard]] std::expected<int, std::string> processOf(const Instance& instance);

    // A process's command line, environment as it started, and current folder, from /proc.
    // Readable only for the user's own processes.
    [[nodiscard]] std::expected<configuration::Launch, std::string> readLaunch(int processId);

    // How the running Hyprland was started.
    [[nodiscard]] std::expected<configuration::Launch, std::string> launchOf(const Instance& instance);

}
