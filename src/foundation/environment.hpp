// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <optional>
#include <string>

namespace foundation {

    // Environment variables by name.
    using Environment = std::map<std::string, std::string>;

    // Lantern's own environment.
    [[nodiscard]] Environment currentEnvironment();

    [[nodiscard]] std::optional<std::string> variable(const Environment& environment, const std::string& name);

}
