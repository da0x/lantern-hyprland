// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/file-descriptor.hpp"

#include <expected>
#include <optional>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <sys/types.h>

namespace desktop {

    // The user's own live processes whose name (/proc/<pid>/comm) is `name`.
    [[nodiscard]] std::vector<pid_t> findProcesses(std::string_view name, const std::filesystem::path& processRoot = "/proc");

    // Starts `program` the way Hyprland starts one: in a session of its own, with no
    // terminal, and not as Lantern's child, so it keeps running after Lantern exits.
    [[nodiscard]] std::expected<void, std::string> startDetached(const std::filesystem::path& program);

    // Asks the process to exit, as closing it would.
    [[nodiscard]] std::expected<void, std::string> stopProcess(pid_t process);

    // Starts a command line through /bin/sh, detached as startDetached does, and says the
    // process it became, so that exactly that process can be stopped later.
    [[nodiscard]] std::expected<pid_t, std::string> startCommand(const std::string& command);

    // A running process's name, as the system keeps it (at most 15 characters), or nothing
    // when it has gone.
    [[nodiscard]] std::optional<std::string> processName(pid_t process, const std::filesystem::path& processRoot = "/proc");

    // Runs `program` with `arguments`, waits for it, and returns what it printed. For quick
    // reads such as gsettings get; anything slow belongs in application::runCommand.
    [[nodiscard]] std::expected<std::string, std::string> readCommand(const std::filesystem::path& program, const std::vector<std::string>& arguments);

    // Sends `signal` to the user's processes called `name`, as asking them to read their
    // config again (kitty reloads it on SIGUSR1). Returns how many it reached.
    std::size_t signalProcesses(std::string_view name, int signal);

    // A descriptor that becomes readable when the process exits.
    [[nodiscard]] std::expected<foundation::FileDescriptor, std::string> exitOf(pid_t process);

}
