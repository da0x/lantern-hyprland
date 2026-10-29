// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace foundation {

    // A key's modifiers, as the bit mask Hyprland reports (xkb's modifier order).
    class Modifiers {
      public:
        enum class Modifier : std::uint32_t {
            shift   = 1U << 0U,
            caps    = 1U << 1U,
            control = 1U << 2U,
            alt     = 1U << 3U,
            mod2    = 1U << 4U,
            mod3    = 1U << 5U,
            super   = 1U << 6U,
            mod5    = 1U << 7U,
        };

        constexpr Modifiers() = default;
        constexpr explicit Modifiers(std::uint32_t mask) : mask{mask} {}

        [[nodiscard]] constexpr bool contains(Modifier modifier) const { return (mask & static_cast<std::uint32_t>(modifier)) != 0; }
        [[nodiscard]] constexpr bool empty() const { return mask == 0; }

        // The modifiers and then the key, the way Hyprland's Lua config writes them:
        // "SUPER + SHIFT + S". Without modifiers, the key alone.
        [[nodiscard]] std::string with(std::string_view key) const;

        friend constexpr bool     operator==(Modifiers, Modifiers) = default;

      private:
        std::uint32_t mask = 0;
    };

}
