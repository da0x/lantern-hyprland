// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace ipc {

    // Which of its two config languages Hyprland is running.
    enum class ConfigFormat {
        lua,
        // hyprland.conf.
        hyprlang,
        unknown,
    };

    // Reads the reply to Query::status.
    [[nodiscard]] std::expected<ConfigFormat, std::string> parseConfigFormat(std::string_view reply);

}
