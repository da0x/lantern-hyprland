// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/loaded-config.hpp"

#include "ipc/launch.hpp"
#include "ipc/request.hpp"
#include "ipc/status.hpp"

#include <format>

namespace application {

    LoadedConfig findLoadedConfig(const ipc::Instance& instance) {
        LoadedConfig loaded{.config = std::unexpected{std::string{}}, .doubts = {}};

        auto launch = ipc::launchOf(instance);
        if (!launch) {
            loaded.doubts.push_back(std::format("Lantern could not read how Hyprland was started ({}), so it looked where Hyprland would from Lantern's own environment.", launch.error()));
            launch = configuration::lanternLaunch();
        }

        loaded.config = configuration::mainConfig(*launch);
        if (!loaded.config)
            return loaded;
        const auto& config = *loaded.config;

        // Hyprland chooses its config once, at startup. Its running language shows whether
        // the files have changed since in a way that matters.
        const auto reply  = ipc::request(instance, ipc::Query::status);
        const auto format = reply ? ipc::parseConfigFormat(*reply) : std::expected<ipc::ConfigFormat, std::string>{std::unexpect, reply.error()};
        if (format && *format == ipc::ConfigFormat::hyprlang && config.lua()) {
            loaded.config = std::unexpected{std::format("Hyprland is running a hyprland.conf, which Lantern does not read yet. It would choose {} if it started now", config.file.string())};
            return loaded;
        }
        if (format && *format == ipc::ConfigFormat::lua && !config.lua()) {
            loaded.config = std::unexpected{std::format("Hyprland is running a Lua config, but the one it would choose now is {}. Its Lua config may have moved since it started", config.file.string())};
            return loaded;
        }
        if (!config.lua()) {
            loaded.config = std::unexpected{std::format("Hyprland reads {}, a hyprland.conf, which Lantern does not read yet", config.file.string())};
            return loaded;
        }

        if (config.safeMode)
            loaded.doubts.emplace_back("Hyprland was started in safe mode, so it may be running its recovery config rather than this one.");
        return loaded;
    }

}
