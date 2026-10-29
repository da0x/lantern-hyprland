// SPDX-License-Identifier: GPL-3.0-or-later
#include "ipc/status.hpp"

#include <nlohmann/json.hpp>

namespace ipc {

    std::expected<ConfigFormat, std::string> parseConfigFormat(std::string_view reply) {
        const auto document = nlohmann::json::parse(reply, nullptr, false);
        if (document.is_discarded() || !document.is_object())
            return std::unexpected{"the status reply is not a JSON object"};

        const auto provider = document.find("configProvider");
        if (provider == document.end() || !provider->is_string())
            return ConfigFormat::unknown;
        const auto name = provider->get<std::string>();
        if (name == "lua")
            return ConfigFormat::lua;
        if (name == "hyprlang")
            return ConfigFormat::hyprlang;
        return ConfigFormat::unknown;
    }

}
