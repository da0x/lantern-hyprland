// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/process.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstring>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>

#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ; // NOLINT(readability-redundant-declaration): POSIX declares it nowhere else

namespace desktop {

    namespace {

        std::string readFirstLine(const std::filesystem::path& file) {
            std::ifstream stream{file};
            std::string   line;
            std::getline(stream, line);
            return line;
        }

        // The state letter in /proc/<pid>/stat, which follows the name in parentheses. The
        // name can hold anything, ")" included, so the last one ends it.
        char stateOf(const std::filesystem::path& folder) {
            const auto line  = readFirstLine(folder / "stat");
            const auto close = line.rfind(')');
            return close != std::string::npos && close + 2 < line.size() ? line[close + 2] : '?';
        }

        std::string errorText(int number) {
            return std::strerror(number); // NOLINT(concurrency-mt-unsafe): the UI thread only
        }

    }

    std::vector<pid_t> findProcesses(std::string_view name, const std::filesystem::path& processRoot) {
        std::vector<pid_t> found;
        std::error_code    error;
        for (const auto& entry : std::filesystem::directory_iterator{processRoot, error}) {
            const auto folderName = entry.path().filename().string();
            pid_t      process    = 0;
            const auto [end, parsed] = std::from_chars(folderName.data(), folderName.data() + folderName.size(), process);
            if (parsed != std::errc{} || end != folderName.data() + folderName.size())
                continue;
            struct stat owner{};
            if (::stat(entry.path().c_str(), &owner) != 0 || owner.st_uid != ::getuid())
                continue;
            if (readFirstLine(entry.path() / "comm") != name || stateOf(entry.path()) == 'Z')
                continue;
            found.push_back(process);
        }
        std::ranges::sort(found);
        return found;
    }

    std::expected<void, std::string> startDetached(const std::filesystem::path& program) {
        // Everything the children use is made before forking: between fork and exec only
        // async-signal-safe calls are allowed.
        std::string           name = program.filename().string();
        std::array<char*, 2>  arguments{name.data(), nullptr};
        const pid_t           child = ::fork();
        if (child < 0)
            return std::unexpected{std::format("cannot start {}: {}", name, errorText(errno))};
        if (child == 0) {
            // A session of its own, then a second fork, so the program is nobody's child
            // here and is adopted by the system once this one exits.
            ::setsid();
            if (::fork() != 0)
                ::_exit(0);
            const int nothing = ::open("/dev/null", O_RDWR); // NOLINT(cppcoreguidelines-pro-type-vararg): POSIX
            if (nothing >= 0) {
                ::dup2(nothing, STDIN_FILENO);
                ::dup2(nothing, STDOUT_FILENO);
                ::dup2(nothing, STDERR_FILENO);
            }
            ::execve(program.c_str(), arguments.data(), environ);
            ::_exit(127);
        }
        int status = 0;
        while (::waitpid(child, &status, 0) < 0) {
            if (errno != EINTR)
                return std::unexpected{std::format("cannot start {}: {}", name, errorText(errno))};
        }
        return {};
    }

    std::expected<pid_t, std::string> startCommand(const std::string& command) {
        // The shell becomes the program with exec, so the process started is the program.
        std::string           shell = "/bin/sh";
        std::string           flag  = "-c";
        std::string           line  = "exec " + command;
        std::array<char*, 4>  arguments{shell.data(), flag.data(), line.data(), nullptr};
        std::array<int, 2>    pipe{};
        if (::pipe2(pipe.data(), O_CLOEXEC) != 0)
            return std::unexpected{std::format("cannot start {}: {}", command, errorText(errno))};
        const pid_t child = ::fork();
        if (child < 0)
            return std::unexpected{std::format("cannot start {}: {}", command, errorText(errno))};
        if (child == 0) {
            // As startDetached, but the middle process says which process the program is.
            ::setsid();
            const pid_t program = ::fork();
            if (program != 0) {
                (void)::write(pipe[1], &program, sizeof program);
                ::_exit(0);
            }
            const int nothing = ::open("/dev/null", O_RDWR); // NOLINT(cppcoreguidelines-pro-type-vararg): POSIX
            if (nothing >= 0) {
                ::dup2(nothing, STDIN_FILENO);
                ::dup2(nothing, STDOUT_FILENO);
                ::dup2(nothing, STDERR_FILENO);
            }
            ::execve(shell.c_str(), arguments.data(), environ);
            ::_exit(127);
        }
        ::close(pipe[1]);
        pid_t   program = -1;
        ssize_t got     = 0;
        while ((got = ::read(pipe[0], &program, sizeof program)) < 0 && errno == EINTR) {}
        ::close(pipe[0]);
        int status = 0;
        while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        if (got != sizeof program || program <= 0)
            return std::unexpected{std::format("cannot start {}", command)};
        return program;
    }

    std::optional<std::string> processName(pid_t process, const std::filesystem::path& processRoot) {
        std::ifstream stream{processRoot / std::to_string(process) / "comm"};
        std::string   name;
        if (!std::getline(stream, name))
            return std::nullopt;
        return name;
    }

    std::expected<std::string, std::string> readCommand(const std::filesystem::path& program, const std::vector<std::string>& arguments) {
        std::array<int, 2> pipe{};
        if (::pipe2(pipe.data(), O_CLOEXEC) != 0)
            return std::unexpected{std::format("cannot run {}: {}", program.string(), errorText(errno))};
        std::vector<std::string> words{program.filename().string()};
        words.insert(words.end(), arguments.begin(), arguments.end());
        std::vector<char*> argv;
        for (auto& word : words)
            argv.push_back(word.data());
        argv.push_back(nullptr);
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipe[1], STDOUT_FILENO);
        pid_t      child  = 0;
        const auto failed = ::posix_spawn(&child, program.c_str(), &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        ::close(pipe[1]);
        if (failed != 0) {
            ::close(pipe[0]);
            return std::unexpected{std::format("cannot run {}: {}", program.string(), errorText(failed))};
        }
        std::string            output;
        std::array<char, 4096> buffer{};
        while (true) {
            const auto count = ::read(pipe[0], buffer.data(), buffer.size());
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                break;
            output.append(buffer.data(), static_cast<std::size_t>(count));
        }
        ::close(pipe[0]);
        int status = 0;
        while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            return std::unexpected{std::format("{} failed", program.filename().string())};
        return output;
    }

    std::size_t signalProcesses(std::string_view name, int signal) {
        std::size_t reached = 0;
        for (const auto process : findProcesses(name))
            if (::kill(process, signal) == 0)
                ++reached;
        return reached;
    }

    std::expected<void, std::string> stopProcess(pid_t process) {
        if (::kill(process, SIGTERM) != 0 && errno != ESRCH)
            return std::unexpected{std::format("cannot stop process {}: {}", process, errorText(errno))};
        return {};
    }

    std::expected<foundation::FileDescriptor, std::string> exitOf(pid_t process) {
        const auto descriptor = static_cast<int>(::syscall(SYS_pidfd_open, process, 0));
        if (descriptor < 0)
            return std::unexpected{std::format("cannot watch process {}: {}", process, errorText(errno))};
        return foundation::FileDescriptor{descriptor};
    }

}
