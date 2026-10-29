// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "activation/effects.hpp"
#include "activation/record.hpp"
#include "configuration/bindings.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace activation {

    // What activating did: the record of it, and anything that went wrong, a sentence each.
    struct Activated {
        Record                   record;
        std::vector<std::string> problems;
    };

    // Applies the effects of the allowed areas to the running session: options, rules and
    // keybindings through `hyprctl eval`, programs started and remembered by process. An
    // area with nothing in it is left out of the record. `since` is when, for the record.
    [[nodiscard]] Activated activate(const Effects& effects, const std::vector<Area>& allowed, const std::optional<std::filesystem::path>& hyprctl, std::string since);

    // Takes back what `record` says is active: Hyprland reloads its config, which drops every
    // change made at run time, and each program Lantern started is stopped, only if the
    // process still has the name it had. Says what went wrong, a sentence each.
    [[nodiscard]] std::vector<std::string> deactivate(const Record& record, const std::optional<std::filesystem::path>& hyprctl, const std::filesystem::path& processRoot = "/proc");

    // Each keybinding effect whose keys the user's config already binds, as "SUPER + K is
    // also bound in hyprland.lua:42".
    [[nodiscard]] std::vector<std::string> collisions(const Effects& effects, const configuration::Bindings& own);

    // The name a program's process has: its command's first word, without its folder, cut
    // to the 15 characters the system keeps.
    [[nodiscard]] std::string processNameOf(const std::string& command);

}
