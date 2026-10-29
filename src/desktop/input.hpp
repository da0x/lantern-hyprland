// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "configuration/file-change.hpp"
#include "configuration/main-config.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace desktop {

    // A keyboard layout, by xkb's names: "us", or "ara" with the variant "qwerty".
    struct Layout {
        std::string code;
        std::string variant;

        friend bool operator==(const Layout&, const Layout&) = default;
    };

    // The keyboard, the mouse and the touchpad: each a value in hl.config's input table, but
    // for the swipe, which is an hl.gesture call.
    struct InputSettings {
        std::vector<Layout>      layouts{{"us", ""}};
        // The xkb options that pick the keys switching layouts ("grp:alt_shift_toggle") and
        // what Caps Lock does ("caps:escape"), empty for xkb's own; and every other option
        // the config has, kept as it is.
        std::string              switchKeys;
        std::string              capsLock;
        std::vector<std::string> otherOptions;
        // Keys held down repeat this many times a second, after this many milliseconds.
        int                      repeatRate      = 25;
        int                      repeatDelay     = 600;
        // The pointer's speed, from -1 to 1; 0 leaves it as the mouse reports it.
        double                   sensitivity     = 0;
        // No acceleration: the pointer moves as far as the mouse, however fast.
        bool                     flat            = false;
        bool                     leftHanded      = false;
        bool                     numLock         = false;
        // Scrolling moves the content the way the fingers or the wheel go, as on a phone.
        bool                     mouseNatural    = false;

        // The touchpad: tapping clicks, typing turns it off for a moment, and a click with two
        // fingers is a right click (otherwise it is a click in the bottom-right corner).
        bool                     touchpadNatural = false;
        bool                     tapToClick      = true;
        bool                     typingDisables  = true;
        bool                     twoFingerClick  = false;
        // How far the content scrolls for each movement; 1 as libinput reports it.
        double                   touchpadScroll  = 1;
        // Three fingers swiped sideways go to the workspace beside this one.
        bool                     swipeWorkspaces = false;

        friend bool operator==(const InputSettings&, const InputSettings&) = default;
    };

    // The values the Hyprland config sets, with Hyprland's own default for any it does not
    // set. Nothing for a config that is not Lua, or that cannot be read.
    [[nodiscard]] std::optional<InputSettings> currentInput(const configuration::MainConfig& config);

    // The change to the Hyprland config (a Lua one; a linked config is followed) that sets
    // `wanted`, writing only what differs from what it sets now, or nothing when nothing does.
    [[nodiscard]] std::expected<std::optional<configuration::FileChange>, std::string> planInput(const InputSettings& wanted, const configuration::MainConfig& config);

    // A layout or variant xkb knows, and what it is called.
    struct KnownLayout {
        std::string code;
        std::string name;
        // Its variants, by their own code and name.
        std::vector<KnownLayout> variants;
    };

    // The layouts in an xkb rules list, such as evdev.lst, by name.
    [[nodiscard]] std::vector<KnownLayout> parseLayouts(std::string_view list);

    // Whether a touchpad is connected, by the devices udev has found in `data`.
    [[nodiscard]] bool touchpadConnected(const std::filesystem::path& data = "/run/udev/data");

    // The layouts xkeyboard-config installs, or none when it is not installed.
    [[nodiscard]] std::vector<KnownLayout> installedLayouts(const std::filesystem::path& list = "/usr/share/X11/xkb/rules/evdev.lst");

}
