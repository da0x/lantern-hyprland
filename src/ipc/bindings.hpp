// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/modifiers.hpp"

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace ipc {

    // One active keybinding, as Hyprland reports it.
    struct Binding {
        foundation::Modifiers modifiers;
        std::string           key;
        std::string           submap;

        // Under a Lua config these say nothing: the dispatcher is "__lua" and the argument
        // is an internal number. See docs/upstream.md.
        std::string dispatcher;
        std::string argument;
        std::string description;

        bool        mouse          = false;
        bool        release        = false;
        bool        repeat         = false;
        bool        locked         = false;
        bool        longPress      = false;
        bool        nonConsuming   = false;

        [[nodiscard]] bool fromLua() const { return dispatcher == "__lua"; }

        friend bool        operator==(const Binding&, const Binding&) = default;
    };

    // Parses the reply to the bindings query, in Hyprland's order.
    [[nodiscard]] std::expected<std::vector<Binding>, std::string> parseBindings(std::string_view reply);

}
