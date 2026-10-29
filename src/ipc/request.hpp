// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ipc/instance.hpp"

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ipc {

    // Every request Lantern may send outside the activation layer. Each one only reads,
    // and nothing else can be expressed here: a request that changes Hyprland belongs to
    // the activation layer and does not go through this function.
    enum class Query {
        bindings,
        clients,
        descriptions,
        monitors,
        status,
        workspaces,
    };

    // Sends one query and waits for the whole JSON reply. Blocks, so never call it on the
    // UI thread.
    [[nodiscard]] std::expected<std::string, std::string> request(const Instance& instance, Query query);

    // Asks for the value of each option in `names` in one batch, for parseOptionValues.
    // Only reads. A name that is not an option's, by its letters, is refused before
    // anything is sent, so nothing but getoption can be smuggled into the batch. Blocks.
    [[nodiscard]] std::expected<std::string, std::string> requestOptions(const Instance& instance, const std::vector<std::string>& names);

    // A request the console may send, by hyprctl's name for it, and what its reply tells.
    struct ConsoleRequest {
        std::string_view name;
        std::string_view tells;
    };

    // Every request the console may send. Each one only reads.
    [[nodiscard]] std::span<const ConsoleRequest> consoleRequests();

    // What the console sends for `typed`: a request from consoleRequests(), or getoption and
    // an option's name, written as for hyprctl, with or without "hyprctl" and "-j". Anything
    // else is refused, with the reason.
    [[nodiscard]] std::expected<std::string, std::string> consoleCommand(std::string_view typed);

    // Sends consoleCommand(typed) and waits for the whole reply. Blocks.
    [[nodiscard]] std::expected<std::string, std::string> requestTyped(const Instance& instance, std::string_view typed);

    // A JSON reply indented the same way throughout, in Hyprland's order; nothing for a
    // reply that is not JSON.
    [[nodiscard]] std::optional<std::string> indentedJson(std::string_view reply);

}
