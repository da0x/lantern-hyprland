// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/environment.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace desktop {

    // The screens turn warmer in the evening and back in the morning, through hyprsunset.
    struct NightLight {
        // In kelvin: 6500 is daylight, lower is warmer.
        int         temperature = 4500;
        // When it turns on and off, as "21:00".
        std::string evening     = "20:00";
        std::string morning     = "07:00";

        friend bool operator==(const NightLight&, const NightLight&) = default;
    };

    // $XDG_CONFIG_HOME/hypr/hyprsunset.conf, or ~/.config/hypr/hyprsunset.conf.
    [[nodiscard]] std::filesystem::path hyprsunsetConfigFile(const foundation::Environment& environment);

    // The night light a hyprsunset.conf sets: a profile with a temperature for the evening,
    // and one with identity, the screens as they are, for the morning. Nothing for a file
    // without that pair.
    [[nodiscard]] std::optional<NightLight> nightLightIn(std::string_view config);

    // `config` with its profiles replaced by the morning's and the evening's, and every
    // other line, such as max-gamma, kept.
    [[nodiscard]] std::string withNightLight(std::string_view config, const NightLight& light);

}
