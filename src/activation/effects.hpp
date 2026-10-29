// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace activation {

    // What activating can change, each allowed on its own. Nothing in an area is applied
    // until the user allows it.
    enum class Area {
        // Hyprland's options and its window, layer and workspace rules.
        options,
        keybindings,
        // Programs the user installed, started by Lantern and stopped again.
        programs,
        // The desktop theme in dconf.
        theme,
    };

    inline constexpr std::array areas{Area::options, Area::keybindings, Area::programs, Area::theme};

    // The word Lantern's settings keep for an area, and back.
    [[nodiscard]] std::string_view    nameOf(Area area);
    [[nodiscard]] std::optional<Area> areaNamed(std::string_view name);

    // One statement of the effects file, in the area it belongs to.
    struct Effect {
        Area        area = Area::options;
        // Its Lua, as written.
        std::string source;
        int         line = 0;
        // A keybinding's keys, as "SUPER + K".
        std::string keys;
        // A program's command line.
        std::string command;
    };

    // A statement Lantern leaves out, and why.
    struct Skipped {
        int         line = 0;
        std::string reason;
    };

    struct Effects {
        std::vector<Effect>  effects;
        std::vector<Skipped> skipped;

        [[nodiscard]] std::vector<const Effect*> in(Area area) const;
    };

    // The effects in an effects file: hl.* calls whose effect Lantern can take back.
    // Options and rules go away with a reload, keybindings too, and a program started by
    // hl.exec_cmd with a plain command line is one Lantern starts and stops itself.
    // Anything else (a local, a function, hl.env, which a reload does not take back) is
    // skipped, and said so.
    [[nodiscard]] Effects readEffects(std::string_view text);

    // $lanternFolder/effects.lua.
    [[nodiscard]] std::filesystem::path effectsFile(const std::filesystem::path& lanternFolder);

    // What a new effects file says, every example commented out.
    [[nodiscard]] std::string_view effectsTemplate();

    // The Lua that applies an area's effects, one statement a line, for hyprctl eval.
    [[nodiscard]] std::string luaFor(const Effects& effects, Area area);

}
