// SPDX-License-Identifier: GPL-3.0-or-later
#include "ipc/launch.hpp"

#include <charconv>
#include <format>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>

namespace ipc {

    namespace {

        // The NUL-separated entries of a /proc file.
        std::expected<std::vector<std::string>, std::string> entries(const std::filesystem::path& file) {
            std::ifstream stream{file, std::ios::binary};
            if (!stream)
                return std::unexpected{std::format("cannot read {}", file.string())};
            std::ostringstream text;
            text << stream.rdbuf();
            const auto               all = std::move(text).str();

            std::vector<std::string> parts;
            std::string_view         rest = all;
            while (!rest.empty()) {
                const auto end = rest.find('\0');
                parts.emplace_back(rest.substr(0, end));
                rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
            }
            return parts;
        }

    }

    std::expected<int, std::string> processOf(const Instance& instance) {
        const auto    file = instance.folder / "hyprland.lock";
        std::ifstream stream{file};
        std::string   line;
        if (!stream || !std::getline(stream, line))
            return std::unexpected{std::format("cannot read {}", file.string())};

        int        processId = 0;
        const auto parsed    = std::from_chars(line.data(), line.data() + line.size(), processId);
        if (parsed.ec != std::errc{} || parsed.ptr != line.data() + line.size() || processId <= 0)
            return std::unexpected{std::format("{} does not start with a process ID", file.string())};
        return processId;
    }

    std::expected<configuration::Launch, std::string> readLaunch(int processId) {
        const auto folder    = std::filesystem::path{"/proc"} / std::to_string(processId);
        auto       arguments = entries(folder / "cmdline");
        if (!arguments)
            return std::unexpected{arguments.error()};
        const auto environment = entries(folder / "environ");
        if (!environment)
            return std::unexpected{environment.error()};

        configuration::Launch launch;
        if (!arguments->empty())
            launch.arguments.assign(std::next(arguments->begin()), arguments->end());
        for (const auto& entry : *environment) {
            const auto equals = entry.find('=');
            if (equals != std::string::npos)
                launch.environment.emplace(entry.substr(0, equals), entry.substr(equals + 1));
        }
        std::error_code error;
        launch.workingFolder = std::filesystem::read_symlink(folder / "cwd", error);
        if (error)
            return std::unexpected{std::format("cannot read the current folder of process {}: {}", processId, error.message())};
        return launch;
    }

    std::expected<configuration::Launch, std::string> launchOf(const Instance& instance) {
        return processOf(instance).and_then(readLaunch);
    }

}
