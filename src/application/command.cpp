// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/command.hpp"

#include "desktop/process.hpp"
#include "foundation/file-descriptor.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <format>
#include <memory>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ; // NOLINT(readability-redundant-declaration): POSIX declares it nowhere else

namespace application {

    namespace {

        // What a running command needs until it exits.
        struct Running {
            pid_t                                                   child = 0;
            foundation::FileDescriptor                              output;
            foundation::FileDescriptor                              exit;
            std::function<void(bool succeeded, std::string output)> done;
        };

        std::string errorText(int number) {
            return std::strerror(number); // NOLINT(concurrency-mt-unsafe): the UI thread only
        }

    }

    std::expected<void, std::string> runCommand(const EventLoop& loop, const std::filesystem::path& program, const std::vector<std::string>& arguments,
                                                std::function<void(bool succeeded, std::string output)> done) {
        std::array<int, 2> pipe{};
        if (::pipe2(pipe.data(), O_CLOEXEC) != 0)
            return std::unexpected{std::format("cannot run {}: {}", program.string(), errorText(errno))};
        foundation::FileDescriptor reading{pipe[0]};
        foundation::FileDescriptor writing{pipe[1]};

        // posix_spawn's argument list is a C array of mutable strings.
        std::vector<std::string> words{program.filename().string()};
        words.insert(words.end(), arguments.begin(), arguments.end());
        std::vector<char*> argv;
        for (auto& word : words)
            argv.push_back(word.data());
        argv.push_back(nullptr);

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, writing.get(), STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, writing.get(), STDERR_FILENO);
        auto       running = std::make_shared<Running>();
        const auto failed  = ::posix_spawn(&running->child, program.c_str(), &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        if (failed != 0)
            return std::unexpected{std::format("cannot run {}: {}", program.string(), errorText(failed))};
        writing = {};

        auto exit = desktop::exitOf(running->child);
        if (!exit) {
            ::waitpid(running->child, nullptr, 0);
            return std::unexpected{exit.error()};
        }
        running->output = std::move(reading);
        running->exit   = std::move(*exit);
        running->done   = std::move(done);

        const int watched = running->exit.get();
        loop.watch(watched, [loop, running] {
            // Unwatching destroys this function, captures and all, so what it needs is
            // copied out first.
            const auto events = loop;
            const auto state  = running;
            events.unwatch(state->exit.get());
            std::string             output;
            std::array<char, 4096>  buffer{};
            ::fcntl(state->output.get(), F_SETFL, O_NONBLOCK); // NOLINT(cppcoreguidelines-pro-type-vararg): POSIX
            while (true) {
                const auto count = ::read(state->output.get(), buffer.data(), buffer.size());
                if (count <= 0)
                    break;
                output.append(buffer.data(), static_cast<std::size_t>(count));
            }
            int status = 0;
            ::waitpid(state->child, &status, 0);
            const bool succeeded = WIFEXITED(status) && WEXITSTATUS(status) == 0;
            if (state->done)
                state->done(succeeded, std::move(output));
        });
        return {};
    }

}
