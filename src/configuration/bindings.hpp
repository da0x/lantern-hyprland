// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "configuration/location.hpp"
#include "configuration/recording.hpp"
#include "configuration/value.hpp"
#include "foundation/modifiers.hpp"

#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace configuration {

    // A keybinding as the config made it.
    struct Binding {
        foundation::Modifiers modifiers;
        std::string           key;
        std::string           submap;

        // An hl.dsp.* action, a function, or whatever else the config passed.
        Value                 action;
        std::string           description;
        Location              location;

        bool                  mouse        = false;
        bool                  release      = false;
        bool                  repeat       = false;
        bool                  locked       = false;
        bool                  longPress    = false;
        bool                  nonConsuming = false;
    };

    // Splits "SUPER + SHIFT + S" into its modifiers and its key. Modifier names are the
    // ones Hyprland accepts, in any case.
    [[nodiscard]] std::expected<std::pair<foundation::Modifiers, std::string>, std::string> parseKeys(std::string_view keys);

    // Every hl.bind call in the recording, in the order the config made them. A call whose
    // keys cannot be read is skipped and named in `unreadable`.
    struct Bindings {
        std::vector<Binding>     bindings;
        std::vector<std::string> unreadable;
    };
    [[nodiscard]] Bindings bindings(const Recording& recording);

}
