// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "desktop/waybar-style.hpp"
#include "foundation/environment.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace desktop {

    // The volume and brightness pop-ups, shown by swayosd when the keys for them are
    // pressed: its server starts at login, and the keys ask its client to change the
    // volume or brightness and show it.

    // ~/.config/swayosd/style.css, which swayosd's server reads over its own style.
    [[nodiscard]] std::filesystem::path popupStyleFile(const foundation::Environment& environment);

    // A style in the bar's colors. Its first line says Lantern wrote it.
    [[nodiscard]] std::string popupStyle(const Palette& palette);

    // Whether Lantern wrote the style, so that it may write it again.
    [[nodiscard]] bool lanternPopupStyle(std::string_view style);

    // Whether a Lua config starts swayosd's server at login.
    [[nodiscard]] bool popupsIn(std::string_view config);

    // Whether a Lua config has Super with the number pad's + and − run swayosd's client,
    // for keyboards without volume keys.
    [[nodiscard]] bool numpadVolumeIn(std::string_view config);

    // A Lua config with the pop-ups on: the server started at login, and the volume keys,
    // with `brightness` the brightness keys, and with `numpad` Super with the number pad's
    // + and −, running swayosd's client. Off, the server no longer starts, keys that ran its
    // client run wpctl and brightnessctl again, as in Hyprland's example config, and the
    // number pad's keys are unbound.
    [[nodiscard]] std::string withPopups(std::string_view config, bool on, bool brightness, bool numpad);

}
