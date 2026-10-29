// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/source-file.hpp"
#include "configuration/bindings.hpp"
#include "configuration/recording.hpp"
#include "ipc/bindings.hpp"

#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace application {

    // One active binding, and the line of the config that made it when Lantern found one.
    struct KeybindingRow {
        ipc::Binding                          live;
        std::optional<configuration::Binding> recorded;
    };

    // What the Keybindings page shows: Hyprland says which bindings are active; the config
    // says what each one does and where it was set.
    struct Keybindings {
        std::vector<KeybindingRow>          rows;

        // Made by the config but not active in Hyprland. Usually the config changed and
        // Hyprland has not reloaded it, or a later call replaced or removed the binding.
        std::vector<configuration::Binding> inactive;

        // Why the config could not be read, or where reading it stopped. Empty when it was
        // read to the end.
        std::string                         configurationProblem;

        // What Lantern could not be sure of about which config Hyprland loaded.
        std::vector<std::string>            configurationDoubts;

        // Every file the config loaded, main file first, as it was when the bindings were read.
        std::vector<SourceFile>             sources;
    };

    // The active bindings that share their keys with another in the same submap, triggered
    // the same way: on press, on release or on a long press. Each group holds indexes into
    // `rows`, in order. Keys bound once are in no group.
    [[nodiscard]] std::vector<std::vector<std::size_t>> clashes(const std::vector<KeybindingRow>& rows);

    // Pairs each active binding with the first binding the config made for the same keys
    // and submap that is not already taken, so duplicates pair up in order.
    [[nodiscard]] Keybindings joinKeybindings(std::vector<ipc::Binding> live, const std::expected<configuration::Recording, std::string>& recording);

}
