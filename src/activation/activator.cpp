// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation/activator.hpp"

#include "desktop/process.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>

namespace activation {

    namespace {

        std::string lowered(std::string text) {
            std::ranges::transform(text, text.begin(), [](unsigned char letter) { return static_cast<char>(std::tolower(letter)); });
            return text;
        }

        // hyprctl's answer, "ok" or "error: ..." with a line break or two.
        std::string trimmed(std::string text) {
            while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
                text.pop_back();
            return text;
        }

    }

    std::string processNameOf(const std::string& command) {
        const auto first = command.substr(0, command.find(' '));
        const auto name  = std::filesystem::path{first}.filename().string();
        return name.substr(0, 15);
    }

    Activated activate(const Effects& effects, const std::vector<Area>& allowed, const std::optional<std::filesystem::path>& hyprctl, std::string since) {
        Activated done;
        done.record.since = std::move(since);
        for (const auto area : allowed) {
            const auto inArea = effects.in(area);
            if (inArea.empty())
                continue;
            switch (area) {
                case Area::options:
                case Area::keybindings: {
                    if (!hyprctl) {
                        done.problems.emplace_back("hyprctl is not installed, so nothing could be sent to Hyprland.");
                        continue;
                    }
                    // Recorded before it is sent, so that a half-applied area is still taken back.
                    done.record.areas.push_back(area);
                    const auto answer = desktop::readCommand(*hyprctl, {"eval", luaFor(effects, area)});
                    if (!answer)
                        done.problems.push_back(answer.error());
                    else if (const auto said = trimmed(*answer); said != "ok")
                        done.problems.push_back(std::format("Hyprland said: {}", said));
                    break;
                }
                case Area::programs: {
                    done.record.areas.push_back(area);
                    for (const auto* effect : inArea) {
                        const auto started = desktop::startCommand(effect->command);
                        if (!started)
                            done.problems.push_back(started.error());
                        else
                            done.record.programs.push_back({.process = *started, .name = processNameOf(effect->command), .command = effect->command});
                    }
                    break;
                }
                case Area::theme:
                    // The effects file holds no theme values yet.
                    break;
            }
        }
        return done;
    }

    std::vector<std::string> deactivate(const Record& record, const std::optional<std::filesystem::path>& hyprctl, const std::filesystem::path& processRoot) {
        std::vector<std::string> problems;
        const bool reload = std::ranges::any_of(record.areas, [](Area area) { return area == Area::options || area == Area::keybindings; });
        if (reload) {
            if (!hyprctl) {
                problems.emplace_back("hyprctl is not installed, so Hyprland could not be asked to reload.");
            } else if (const auto answer = desktop::readCommand(*hyprctl, {"reload"}); !answer) {
                problems.push_back(answer.error());
            } else if (const auto said = trimmed(*answer); said != "ok") {
                problems.push_back(std::format("Hyprland said: {}", said));
            }
        }
        for (const auto& program : record.programs) {
            // A process that has gone, or whose number another program now has, is left.
            if (desktop::processName(program.process, processRoot) != program.name)
                continue;
            if (const auto stopped = desktop::stopProcess(program.process); !stopped)
                problems.push_back(stopped.error());
        }
        return problems;
    }

    std::vector<std::string> collisions(const Effects& effects, const configuration::Bindings& own) {
        std::vector<std::string> found;
        for (const auto* effect : effects.in(Area::keybindings)) {
            const auto keys = configuration::parseKeys(effect->keys);
            if (!keys)
                continue;
            for (const auto& binding : own.bindings)
                if (binding.submap.empty() && binding.modifiers == keys->first && lowered(binding.key) == lowered(keys->second)) {
                    found.push_back(std::format("{} is also bound in {}", effect->keys, binding.location.brief()));
                    break;
                }
        }
        return found;
    }

}
