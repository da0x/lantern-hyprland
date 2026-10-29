// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/config-check.hpp"

#include "application/command.hpp"
#include "configuration/file-change.hpp"

#include <chrono>
#include <format>

namespace application {

    void checkHyprlandConfig(const EventLoop& loop, const std::filesystem::path& hyprctl, const std::filesystem::path& record,
                             std::function<void(std::optional<std::string>)> done) {
        // Hyprland reads its config again when the file changes; give it a moment.
        loop.after(std::chrono::milliseconds{1500}, [loop, hyprctl, record, done = std::move(done)] {
            const auto started = runCommand(loop, hyprctl, {"configerrors"}, [record, done](bool, std::string output) {
                const auto first = output.find_first_not_of(" \n\t");
                if (first == std::string::npos)
                    return done(std::nullopt);
                const auto line = output.substr(first, output.find('\n', first) - first);
                if (const auto undone = configuration::undoChange(record); undone)
                    done(std::format("Hyprland found errors in the changed config, so the files were put back: {}", line));
                else
                    done(std::format("Hyprland found errors in the changed config, and the files could not be put back: {}", undone.error()));
            });
            if (!started)
                done(std::format("Lantern could not ask Hyprland whether the config has errors: {}", started.error()));
        });
    }

}
