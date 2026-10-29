// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ipc {

    // One of Hyprland's options as it documents itself.
    struct OptionDescription {
        // "decoration:blur:enabled".
        std::string                                 name;
        std::string                                 description;
        // Written as the config would write it: "true", "5", "0.5", "5 5 5 5".
        std::string                                 defaultValue;
        std::optional<double>                       minimum;
        std::optional<double>                       maximum;
        // The names a number stands for, smallest number first: {"disabled", 0}, ...
        std::vector<std::pair<std::string, long>>   choices;

        friend bool operator==(const OptionDescription&, const OptionDescription&) = default;
    };

    // An option's value right now, and whether the config set it.
    struct OptionValue {
        std::string name;
        // Empty when Hyprland could not say.
        std::string value;
        bool        set = false;

        friend bool operator==(const OptionValue&, const OptionValue&) = default;
    };

    // Reads the reply to Query::descriptions, in Hyprland's order. Its "current" field is
    // left alone: for some options it is not the value in use.
    [[nodiscard]] std::expected<std::vector<OptionDescription>, std::string> parseOptionDescriptions(std::string_view reply);

    // Reads the reply to requestOptions for `names`, one value per name in the same order.
    [[nodiscard]] std::vector<OptionValue> parseOptionValues(std::string_view reply, const std::vector<std::string>& names);

    // A number as the config would write it: 1 for 1.000000, 0.5 for 0.500000.
    [[nodiscard]] std::string numberText(double number);

}
