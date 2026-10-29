// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace ipc {

    // The two sockets of one running Hyprland, in its own runtime folder.
    struct Instance {
        std::filesystem::path folder;
        std::filesystem::path requestSocket;
        std::filesystem::path eventSocket;
    };

    // The Hyprland this process runs under, from HYPRLAND_INSTANCE_SIGNATURE.
    [[nodiscard]] std::expected<Instance, std::string> currentInstance();

}
