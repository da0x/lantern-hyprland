// SPDX-License-Identifier: GPL-3.0-or-later
#include "foundation/modifiers.hpp"

#include <array>
#include <utility>

namespace foundation {

    namespace {

        // Written in the order people say them, not in bit order.
        constexpr std::array names{
            std::pair{Modifiers::Modifier::super, std::string_view{"SUPER"}}, std::pair{Modifiers::Modifier::control, std::string_view{"CTRL"}},
            std::pair{Modifiers::Modifier::alt, std::string_view{"ALT"}},     std::pair{Modifiers::Modifier::shift, std::string_view{"SHIFT"}},
            std::pair{Modifiers::Modifier::caps, std::string_view{"CAPS"}},   std::pair{Modifiers::Modifier::mod2, std::string_view{"MOD2"}},
            std::pair{Modifiers::Modifier::mod3, std::string_view{"MOD3"}},   std::pair{Modifiers::Modifier::mod5, std::string_view{"MOD5"}},
        };

    }

    std::string Modifiers::with(std::string_view key) const {
        std::string text;
        for (const auto& [modifier, name] : names) {
            if (!contains(modifier))
                continue;
            text += name;
            text += " + ";
        }
        text += key;
        return text;
    }

}
