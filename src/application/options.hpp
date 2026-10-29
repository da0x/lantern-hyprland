// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "configuration/location.hpp"
#include "configuration/recording.hpp"
#include "ipc/options.hpp"

#include <expected>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace application {

    // One option: what Hyprland says about it and its value, and the call in the config
    // that set it, when Lantern found one.
    struct OptionRow {
        ipc::OptionDescription                 description;
        std::string                            value;
        // Hyprland's word: the config set it, whether or not to its default.
        bool                                   set = false;
        std::optional<configuration::Location> where;

        // Set to something other than the default. Hyprland writes some defaults
        // differently from the values it reports, so both are compared as written.
        [[nodiscard]] bool changed() const { return set && value != description.defaultValue; }
    };

    // What the Options page shows.
    struct Options {
        std::vector<OptionRow> rows;
        // Why the config could not be read, so where options were set is unknown. Empty
        // when it was read.
        std::string            configurationProblem;
    };

    // Where the config last set each option, by its name, from its hl.config calls:
    // hl.config({ decoration = { blur = { enabled = true } } }) sets
    // "decoration:blur:enabled". `names` are the options there are, which says where a
    // name ends and its value, which may itself be a table, begins.
    [[nodiscard]] std::map<std::string, configuration::Location> optionLocations(const configuration::Recording& recording, const std::vector<std::string>& names);

    // Each described option with its value, in Hyprland's order.
    [[nodiscard]] Options joinOptions(std::vector<ipc::OptionDescription> descriptions, const std::vector<ipc::OptionValue>& values,
                                      const std::expected<configuration::Recording, std::string>& recording);

}
