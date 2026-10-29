// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/popups.hpp"

#include "configuration/autostart.hpp"
#include "configuration/lua-binds.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace desktop {

    namespace {

        constexpr std::string_view server = "swayosd-server";
        constexpr std::string_view marker = "/* Written by Lantern for Hyprland, in the bar's colors. */";

        struct PopupKey {
            std::string_view keys;
            std::string_view popup;
            // What the key runs without the pop-ups, as in Hyprland's example config.
            std::string_view plain;
            bool             repeats    = true;
            bool             brightness = false;
        };

        constexpr std::array<PopupKey, 6> popupKeys{{
            {"XF86AudioRaiseVolume", "swayosd-client --output-volume raise", "wpctl set-volume -l 1 @DEFAULT_AUDIO_SINK@ 5%+", true, false},
            {"XF86AudioLowerVolume", "swayosd-client --output-volume lower", "wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-", true, false},
            {"XF86AudioMute", "swayosd-client --output-volume mute-toggle", "wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle", false, false},
            {"XF86AudioMicMute", "swayosd-client --input-volume mute-toggle", "wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle", false, false},
            {"XF86MonBrightnessUp", "swayosd-client --brightness raise", "brightnessctl -e4 -n2 set 5%+", true, true},
            {"XF86MonBrightnessDown", "swayosd-client --brightness lower", "brightnessctl -e4 -n2 set 5%-", true, true},
        }};

        // Lantern's own, for keyboards without volume keys.
        constexpr std::array<PopupKey, 2> numpadKeys{{
            {"SUPER + KP_Add", "swayosd-client --output-volume raise", "", true, false},
            {"SUPER + KP_Subtract", "swayosd-client --output-volume lower", "", true, false},
        }};

        std::string_view optionsOf(const PopupKey& key) {
            return key.repeats ? "{ locked = true, repeating = true }" : "{ locked = true }";
        }

    }

    std::filesystem::path popupStyleFile(const foundation::Environment& environment) {
        if (const auto home = foundation::variable(environment, "XDG_CONFIG_HOME"); home && std::filesystem::path{*home}.is_absolute())
            return std::filesystem::path{*home} / "swayosd" / "style.css";
        return std::filesystem::path{foundation::variable(environment, "HOME").value_or("")} / ".config" / "swayosd" / "style.css";
    }

    std::string popupStyle(const Palette& palette) {
        return std::format(R"({}
window#osd {{
    border-radius: 999px;
    border: 2px solid {};
    background: alpha({}, 0.95);
}}

window#osd #container {{
    margin: 16px;
}}

window#osd image,
window#osd label {{
    color: {};
}}

window#osd progressbar:disabled,
window#osd image:disabled {{
    opacity: 0.5;
}}

window#osd progressbar,
window#osd segmentedprogress {{
    min-height: 6px;
    border-radius: 999px;
    background: transparent;
    border: none;
}}

window#osd trough,
window#osd segment {{
    min-height: inherit;
    border-radius: inherit;
    border: none;
    background: alpha({}, 0.25);
}}

window#osd progress,
window#osd segment.active {{
    min-height: inherit;
    border-radius: inherit;
    border: none;
    background: {};
}}
)",
                           marker, palette.surface, palette.base, palette.text, palette.text, palette.accent);
    }

    bool lanternPopupStyle(std::string_view style) {
        return style.starts_with(marker);
    }

    bool popupsIn(std::string_view config) {
        return configuration::startsAtLogin(config, true, server);
    }

    bool numpadVolumeIn(std::string_view config) {
        const auto binds = configuration::luaBindLines(config);
        return std::ranges::all_of(numpadKeys, [&binds](const PopupKey& key) {
            return std::ranges::any_of(binds, [&key](const configuration::LuaBindLine& bind) {
                return configuration::sameKeys(bind.keys, key.keys) && bind.command && bind.command->starts_with("swayosd-client");
            });
        });
    }

    std::string withPopups(std::string_view config, bool on, bool brightness, bool numpad) {
        std::string text = on ? configuration::withAutostart(config, true, server, {}) : configuration::withoutAutostart(config, true, server);
        const auto  binds = configuration::luaBindLines(text);
        for (const auto& key : popupKeys) {
            const auto bound = std::ranges::find_if(binds, [&](const configuration::LuaBindLine& bind) { return configuration::sameKeys(bind.keys, key.keys); });
            const bool ours  = bound != binds.end() && bound->command && bound->command->starts_with("swayosd-client");
            if (on && (!key.brightness || brightness))
                text = configuration::withLuaExecBind(text, key.keys, key.popup, optionsOf(key));
            else if (!on && ours)
                text = configuration::withLuaExecBind(text, key.keys, key.plain, optionsOf(key));
        }
        for (const auto& key : numpadKeys) {
            const auto bound = std::ranges::find_if(binds, [&](const configuration::LuaBindLine& bind) { return configuration::sameKeys(bind.keys, key.keys); });
            const bool ours  = bound != binds.end() && bound->command && bound->command->starts_with("swayosd-client");
            if (on && numpad)
                text = configuration::withLuaExecBind(text, key.keys, key.popup, optionsOf(key));
            else if (ours)
                text = configuration::withoutLuaBind(text, key.keys);
        }
        return text;
    }

}
