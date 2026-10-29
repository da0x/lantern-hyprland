// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "desktop/waybar-style.hpp"
#include "foundation/environment.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace desktop {

    // What happens while the computer is left alone, in minutes, 0 for never; and whether
    // the screen locks before the computer sleeps. hypridle carries it out. The screensaver
    // is the lock screen started with a grace period that lasts until the lock's time, so a
    // key or the mouse brings the desktop back without the password until then.
    struct IdleSettings {
        int  dim             = 0;
        int  screensaver     = 0;
        int  lock            = 0;
        int  screensOff      = 0;
        int  suspend         = 0;
        bool lockBeforeSleep = false;

        friend bool operator==(const IdleSettings&, const IdleSettings&) = default;
    };

    // $XDG_CONFIG_HOME/hypr/hypridle.conf or hyprlock.conf, or the same in ~/.config.
    [[nodiscard]] std::filesystem::path hypridleConfigFile(const foundation::Environment& environment);
    [[nodiscard]] std::filesystem::path hyprlockConfigFile(const foundation::Environment& environment);

    // The settings a hypridle.conf carries out. Each listener is known by what it runs when
    // the time is up: brightnessctl dims, loginctl lock-session or hyprlock locks, a dpms
    // dispatch turns the screens off, and systemctl suspend suspends.
    [[nodiscard]] IdleSettings idleIn(std::string_view config);

    // `config` carrying out `settings`: the listeners it knows are replaced, the general
    // block gets the commands that lock and wake the screens, and everything else is kept.
    [[nodiscard]] std::string withIdle(std::string_view config, const IdleSettings& settings);

    // How Lantern's lock screen looks: what is behind it, how large its clock is, and
    // whether the date is under the clock.
    enum class LockBackground { desktop, wallpaper, color };
    enum class LockClock { large, small, none };
    struct LockDesign {
        LockBackground background = LockBackground::desktop;
        LockClock      clock      = LockClock::large;
        bool           date       = true;

        friend bool operator==(const LockDesign&, const LockDesign&) = default;
    };

    // A hyprlock.conf in the palette's colors, drawn as `design` says, with a password field.
    // The wallpaper background shows `wallpaper`, or the palette's color without one.
    [[nodiscard]] std::string lockScreen(const Palette& palette, const LockDesign& design = {}, const std::optional<std::filesystem::path>& wallpaper = std::nullopt);

    // Whether hyprlock.conf is one Lantern wrote.
    [[nodiscard]] bool lanternLockScreen(std::string_view config);

    // The design a hyprlock.conf Lantern wrote was drawn with; nothing for any other.
    [[nodiscard]] std::optional<LockDesign> lockDesignIn(std::string_view config);

    // The keys offered for locking the screen at once, as Hyprland writes them: first the
    // lock key some keyboards have, then Scroll Lock, which others send for a lock button.
    [[nodiscard]] std::span<const std::string_view> lockKeys();

    // The keys a Lua config binds to lock the screen at once; empty when none does.
    [[nodiscard]] std::string lockKeyIn(std::string_view config);

    // The config with `keys` locking the screen at once, replacing what they did before, and
    // with no other keys doing it; with none when `keys` is empty.
    [[nodiscard]] std::string withLockKey(std::string_view config, std::string_view keys);

    // A brightness to set: a backlight, and a built-in screen for it to light. Some
    // computers with no built-in screen, such as the 2013 Mac Pro, still list one.
    [[nodiscard]] bool hasBacklight(const std::filesystem::path& classes = "/sys/class");

}
