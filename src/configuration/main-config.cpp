// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/main-config.hpp"

#include "foundation/environment.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <string_view>
#include <system_error>

namespace configuration {

    namespace {

        std::optional<std::string> variable(const Launch& launch, const std::string& name) {
            return foundation::variable(launch.environment, name);
        }

        // Hyprland's options that take a value, so a value that looks like -c is not read as one.
        bool takesValue(std::string_view option) {
            constexpr std::array options{std::string_view{"--socket"}, std::string_view{"--wayland-fd"}, std::string_view{"--watchdog-fd"}, std::string_view{"--locked-cmd"}};
            return std::ranges::contains(options, option);
        }

        // The folders Hyprland searches, in order. Relative ones in XDG_CONFIG_HOME or HOME
        // are skipped, as Hyprland skips them; XDG_CONFIG_DIRS is taken as written.
        std::vector<std::filesystem::path> searchFolders(const Launch& launch, const std::filesystem::path& systemFolder) {
            std::vector<std::filesystem::path> folders;
            if (const auto home = variable(launch, "XDG_CONFIG_HOME"); home && std::filesystem::path{*home}.is_absolute())
                folders.emplace_back(*home);
            if (const auto home = variable(launch, "HOME"); home && std::filesystem::path{*home}.is_absolute())
                folders.push_back(std::filesystem::path{*home} / ".config");
            if (const auto list = variable(launch, "XDG_CONFIG_DIRS")) {
                std::string_view rest = *list;
                while (!rest.empty()) {
                    const auto colon = rest.find(':');
                    const auto entry = rest.substr(0, colon);
                    if (!entry.empty())
                        folders.emplace_back(entry);
                    rest = colon == std::string_view::npos ? std::string_view{} : rest.substr(colon + 1);
                }
            }
            folders.push_back(systemFolder);
            return folders;
        }

    }

    std::expected<MainConfig, std::string> mainConfig(const Launch& launch, const std::filesystem::path& systemFolder) {
        MainConfig                 config;
        std::optional<std::string> fromCommandLine;
        for (std::size_t index = 0; index < launch.arguments.size(); ++index) {
            const auto& argument = launch.arguments[index];
            const bool  hasValue = index + 1 < launch.arguments.size();
            if ((argument == "-c" || argument == "--config") && hasValue)
                fromCommandLine = launch.arguments[++index];
            else if (takesValue(argument) && hasValue)
                ++index;
            else if (argument == "--safe-mode")
                config.safeMode = true;
        }

        std::error_code error;
        if (fromCommandLine) {
            // Hyprland follows links in this path, and only in this one.
            config.file   = std::filesystem::weakly_canonical(launch.workingFolder / *fromCommandLine, error);
            config.choice = Choice::commandLine;
            if (error)
                return std::unexpected{std::format("Hyprland was started with --config {}, which cannot be read: {}", *fromCommandLine, error.message())};
            return config;
        }
        if (const auto fromEnvironment = variable(launch, "HYPRLAND_CONFIG")) {
            config.file   = (launch.workingFolder / *fromEnvironment).lexically_normal();
            config.choice = Choice::environment;
            return config;
        }

        const auto folders = searchFolders(launch, systemFolder);
        for (const auto* name : {"hyprland.lua", "hyprland.conf"}) {
            for (const auto& folder : folders) {
                const auto file = folder / "hypr" / name;
                if (std::filesystem::exists(file, error)) {
                    config.file   = file.lexically_normal();
                    config.choice = Choice::search;
                    return config;
                }
            }
        }
        return std::unexpected{"there is no hyprland.lua or hyprland.conf in any of the folders Hyprland looks in"};
    }

    Launch lanternLaunch() {
        Launch          launch;
        std::error_code error;
        launch.workingFolder = std::filesystem::current_path(error);
        launch.environment   = foundation::currentEnvironment();
        return launch;
    }

}
