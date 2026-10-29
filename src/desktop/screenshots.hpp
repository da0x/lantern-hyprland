// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace desktop {

    // How the screenshot keys work: grim takes the picture, slurp selects an area, and
    // satty, when chosen, opens it to draw on before it is saved.
    struct Screenshots {
        // Where pictures are saved, with ~ for the home folder.
        std::string folder   = "~/Pictures/Screenshots";
        // Each picture is copied to the clipboard too.
        bool        copy     = true;
        bool        annotate = false;

        bool        operator==(const Screenshots&) const = default;
    };

    enum class Shot { area, screens, window };

    struct ShotKey {
        Shot             shot;
        std::string_view keys;
        // What it takes, in words.
        std::string_view about;
    };

    inline constexpr std::array<ShotKey, 3> shotKeys{{
        {Shot::area, "Print", "an area you select"},
        {Shot::screens, "SHIFT + Print", "every screen"},
        {Shot::window, "CTRL + Print", "the window in focus"},
    }};

    // The command a key runs.
    [[nodiscard]] std::string screenshotCommand(const Screenshots& screenshots, Shot shot);

    // The screenshots the Print key of a Lua config takes, when it runs grim: its folder,
    // and whether it copies and annotates.
    [[nodiscard]] std::optional<Screenshots> screenshotsIn(std::string_view config);

    // A Lua config whose three screenshot keys take `screenshots`.
    [[nodiscard]] std::string withScreenshots(std::string_view config, const Screenshots& screenshots);

    // The command that keeps what is copied, started at login.
    inline constexpr std::string_view clipboardWatcher = "wl-paste --watch cliphist store";

    // The command that lists what was copied in `menu`, a launcher's dmenu mode such as
    // "wofi --dmenu", and copies the one picked.
    [[nodiscard]] std::string clipboardPicker(std::string_view menu);

    // The keys that open the clipboard history in a Lua config, if a key does.
    [[nodiscard]] std::optional<std::string> clipboardKeysIn(std::string_view config);

}
