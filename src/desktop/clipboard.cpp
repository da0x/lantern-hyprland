// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/clipboard.hpp"

#include "desktop/system.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <format>

#include <spawn.h>
#include <sys/wait.h>

extern char** environ; // NOLINT(readability-redundant-declaration): POSIX declares it nowhere else

namespace desktop {

    std::optional<std::filesystem::path> clipboardProgram(std::string_view path) {
        return findProgram("wl-copy", path);
    }

    std::expected<void, std::string> copyText(const std::filesystem::path& program, const std::string& text) {
        std::string name      = program.filename().string();
        std::string separator = "--";
        std::string argument  = text;
        // posix_spawn's argument list is a C array of mutable strings.
        std::array<char*, 4> arguments{name.data(), separator.data(), argument.data(), nullptr};

        pid_t      child  = 0;
        const auto failed = ::posix_spawn(&child, program.c_str(), nullptr, nullptr, arguments.data(), environ);
        if (failed != 0)
            return std::unexpected{std::format("cannot run {}: {}", program.string(), std::strerror(failed))}; // NOLINT(concurrency-mt-unsafe): the UI thread only

        int status = 0;
        while (::waitpid(child, &status, 0) < 0) {
            if (errno != EINTR)
                return std::unexpected{std::format("cannot wait for {}: {}", program.string(), std::strerror(errno))}; // NOLINT(concurrency-mt-unsafe): the UI thread only
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            return std::unexpected{std::format("{} could not copy the text", name)};
        return {};
    }

}
