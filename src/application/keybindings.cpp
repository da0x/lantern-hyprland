// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/keybindings.hpp"

#include <algorithm>
#include <cctype>
#include <format>

namespace application {

    namespace {

        bool sameKey(std::string_view left, std::string_view right) {
            return std::ranges::equal(left, right, [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
        }

        bool sameKeys(const ipc::Binding& live, const configuration::Binding& recorded) {
            return live.modifiers == recorded.modifiers && sameKey(live.key, recorded.key) && live.submap == recorded.submap;
        }

    }

    Keybindings joinKeybindings(std::vector<ipc::Binding> live, const std::expected<configuration::Recording, std::string>& recording) {
        Keybindings result;

        std::vector<configuration::Binding> recorded;
        if (!recording) {
            result.configurationProblem = recording.error();
        } else {
            auto read = configuration::bindings(*recording);
            recorded  = std::move(read.bindings);
            if (recording->failure)
                result.configurationProblem = std::format("reading the config stopped at {}: {}",
                                                          recording->failure->stack.empty() ? std::string{"its start"} : recording->failure->stack.front().brief(),
                                                          recording->failure->message);
            else if (!read.unreadable.empty())
                result.configurationProblem = std::format("{} binding(s) could not be read, the first at {}", read.unreadable.size(), read.unreadable.front());
        }

        std::vector<bool> taken(recorded.size(), false);
        for (auto& binding : live) {
            KeybindingRow row{.live = std::move(binding), .recorded = std::nullopt};
            for (std::size_t index = 0; index < recorded.size(); ++index) {
                if (taken[index] || !sameKeys(row.live, recorded[index]))
                    continue;
                taken[index] = true;
                row.recorded = recorded[index];
                break;
            }
            result.rows.push_back(std::move(row));
        }

        for (std::size_t index = 0; index < recorded.size(); ++index)
            if (!taken[index])
                result.inactive.push_back(std::move(recorded[index]));
        return result;
    }

    std::vector<std::vector<std::size_t>> clashes(const std::vector<KeybindingRow>& rows) {
        const auto same = [](const ipc::Binding& one, const ipc::Binding& other) {
            return one.modifiers == other.modifiers && sameKey(one.key, other.key) && one.submap == other.submap && one.mouse == other.mouse &&
                one.release == other.release && one.longPress == other.longPress;
        };
        std::vector<std::vector<std::size_t>> groups;
        std::vector<bool>                     grouped(rows.size(), false);
        for (std::size_t first = 0; first < rows.size(); ++first) {
            if (grouped[first])
                continue;
            std::vector<std::size_t> group{first};
            for (std::size_t other = first + 1; other < rows.size(); ++other)
                if (!grouped[other] && same(rows[first].live, rows[other].live)) {
                    group.push_back(other);
                    grouped[other] = true;
                }
            if (group.size() > 1)
                groups.push_back(std::move(group));
        }
        return groups;
    }

}
