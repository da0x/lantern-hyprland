// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "configuration/main-config.hpp"
#include "ipc/instance.hpp"

#include <expected>
#include <string>
#include <vector>

namespace application {

    // The Lua config the running Hyprland loaded, as near as Lantern can tell.
    struct LoadedConfig {
        // Why there is no Lua config to read, when there is not.
        std::expected<configuration::MainConfig, std::string> config;

        // What Lantern could not be sure of, each a sentence.
        std::vector<std::string>                              doubts;
    };

    // Follows Hyprland's own choice from how it was started, and checks it against the
    // config language Hyprland reports. Blocks on the request socket.
    [[nodiscard]] LoadedConfig findLoadedConfig(const ipc::Instance& instance);

}
