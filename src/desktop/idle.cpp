// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/idle.hpp"

#include "configuration/hyprlang-edit.hpp"
#include "configuration/lua-binds.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <optional>
#include <ranges>
#include <vector>

namespace desktop {

    namespace {

        constexpr auto lockCommand   = "loginctl lock-session";
        constexpr auto screensOn     = R"(hyprctl dispatch 'hl.dsp.dpms({ action = "on" })')";
        constexpr auto screensOffRun = R"(hyprctl dispatch 'hl.dsp.dpms({ action = "off" })')";
        constexpr auto lockMark      = "# Written by Lantern for Hyprland.";
        constexpr auto designMark    = "# Lantern's design:";
        constexpr auto lockNowRun    = "pidof hyprlock || hyprlock";
        // A grace period for a screensaver with no lock after it: a day.
        constexpr int  endlessGrace  = 24 * 60 * 60;

        std::filesystem::path hyprFile(const foundation::Environment& environment, std::string_view name) {
            if (const auto home = foundation::variable(environment, "XDG_CONFIG_HOME"); home && std::filesystem::path{*home}.is_absolute())
                return std::filesystem::path{*home} / "hypr" / name;
            return std::filesystem::path{foundation::variable(environment, "HOME").value_or("")} / ".config" / "hypr" / name;
        }

        std::string valueOf(const configuration::HyprlangBlock& block, std::string_view key) {
            for (const auto& [name, value] : block.values)
                if (name == key)
                    return value;
            return {};
        }

        enum class Role { none, dim, screensaver, lock, screensOff, suspend };

        Role roleOf(const configuration::HyprlangBlock& block) {
            if (block.name != "listener")
                return Role::none;
            const auto run = valueOf(block, "on-timeout");
            if (run.contains("brightnessctl"))
                return Role::dim;
            if (run.contains("--grace"))
                return Role::screensaver;
            if (run.contains("lock-session") || run.contains("hyprlock"))
                return Role::lock;
            if (run.contains("dpms"))
                return Role::screensOff;
            if (run.contains("suspend"))
                return Role::suspend;
            return Role::none;
        }

        int minutesOf(const configuration::HyprlangBlock& block) {
            const auto text    = valueOf(block, "timeout");
            int        seconds = 0;
            std::from_chars(text.data(), text.data() + text.size(), seconds);
            return (seconds + 59) / 60;
        }

    }

    std::filesystem::path hypridleConfigFile(const foundation::Environment& environment) {
        return hyprFile(environment, "hypridle.conf");
    }

    std::filesystem::path hyprlockConfigFile(const foundation::Environment& environment) {
        return hyprFile(environment, "hyprlock.conf");
    }

    IdleSettings idleIn(std::string_view config) {
        IdleSettings settings;
        for (const auto& block : configuration::hyprlangBlocks(config)) {
            if (block.name == "general")
                settings.lockBeforeSleep = valueOf(block, "before_sleep_cmd").contains("lock");
            switch (roleOf(block)) {
                case Role::dim: settings.dim = minutesOf(block); break;
                case Role::screensaver: settings.screensaver = minutesOf(block); break;
                case Role::lock: settings.lock = minutesOf(block); break;
                case Role::screensOff: settings.screensOff = minutesOf(block); break;
                case Role::suspend: settings.suspend = minutesOf(block); break;
                case Role::none: break;
            }
        }
        return settings;
    }

    std::string withIdle(std::string_view config, const IdleSettings& settings) {
        std::string text{config};
        const auto  blocks = configuration::hyprlangBlocks(config);
        // From the end backwards, so that earlier offsets stay right.
        for (const auto& block : blocks | std::views::reverse) {
            if (block.name == "general") {
                std::vector<std::pair<std::string, std::string>> wanted{{"lock_cmd", "pidof hyprlock || hyprlock"}, {"after_sleep_cmd", screensOn}};
                if (settings.lockBeforeSleep)
                    wanted.emplace_back("before_sleep_cmd", lockCommand);
                std::string added;
                for (const auto& [key, value] : wanted)
                    if (valueOf(block, key).empty())
                        added += std::format("    {} = {}\n", key, value);
                // Added at its end first: the lines taken out below come before it.
                text.insert(std::min(block.close, text.size()), added);
                if (!settings.lockBeforeSleep)
                    for (std::size_t index = block.values.size(); index-- > 0;)
                        if (block.values[index].first == "before_sleep_cmd" && block.values[index].second.contains("lock")) {
                            const auto end = text.find('\n', block.valueLines[index]);
                            text.erase(block.valueLines[index], end == std::string::npos ? std::string::npos : end + 1 - block.valueLines[index]);
                        }
                continue;
            }
            if (roleOf(block) == Role::none)
                continue;
            auto begin = block.begin;
            // The blank line that set it apart goes with it.
            if (begin >= 2 && text[begin - 1] == '\n' && text[begin - 2] == '\n')
                --begin;
            text.erase(begin, block.end - begin);
        }
        if (std::ranges::none_of(blocks, [](const auto& block) { return block.name == "general"; })) {
            std::string general = std::format("general {{\n    lock_cmd = pidof hyprlock || hyprlock\n{}    after_sleep_cmd = {}\n}}\n",
                                              settings.lockBeforeSleep ? std::format("    before_sleep_cmd = {}\n", lockCommand) : "", screensOn);
            text = general + (text.empty() ? "" : "\n") + text;
        }

        struct Listener {
            int         minutes;
            std::string body;
        };
        std::vector<Listener> listeners;
        if (settings.dim > 0)
            listeners.push_back({settings.dim, "    on-timeout = brightnessctl -s set 10\n    on-resume = brightnessctl -r\n"});
        if (settings.screensaver > 0) {
            // Until the lock's time, after which the password is needed.
            const auto grace = settings.lock == 0 ? endlessGrace : std::max(0, settings.lock - settings.screensaver) * 60;
            listeners.push_back({settings.screensaver, std::format("    on-timeout = pidof hyprlock || hyprlock --grace {}\n", grace)});
        }
        if (settings.lock > 0)
            listeners.push_back({settings.lock, std::format("    on-timeout = {}\n", lockCommand)});
        if (settings.screensOff > 0)
            listeners.push_back({settings.screensOff, std::format("    on-timeout = {}\n    on-resume = {}\n", screensOffRun, screensOn)});
        if (settings.suspend > 0)
            listeners.push_back({settings.suspend, "    on-timeout = systemctl suspend\n"});
        std::ranges::stable_sort(listeners, {}, &Listener::minutes);
        for (const auto& listener : listeners) {
            if (!text.empty() && !text.ends_with('\n'))
                text += '\n';
            text += std::format("\nlistener {{\n    timeout = {}\n{}}}\n", listener.minutes * 60, listener.body);
        }
        return text;
    }

    std::string lockScreen(const Palette& palette, const LockDesign& design, const std::optional<std::filesystem::path>& wallpaper) {
        const auto rgb = [](std::string_view color) { return std::format("rgb({})", color.starts_with('#') ? color.substr(1) : color); };
        const auto backgroundName = design.background == LockBackground::desktop ? "desktop" : design.background == LockBackground::wallpaper ? "wallpaper" : "color";
        const auto clockName      = design.clock == LockClock::large ? "large" : design.clock == LockClock::small ? "small" : "none";

        std::string background;
        if (design.background == LockBackground::desktop)
            background = "    path = screenshot\n    blur_passes = 3\n    blur_size = 8\n";
        else if (design.background == LockBackground::wallpaper && wallpaper)
            background = std::format("    path = {}\n    blur_passes = 1\n    blur_size = 2\n", wallpaper->string());

        // Above the password field: the clock, and the date under it.
        std::string labels;
        const int   clockSize = design.clock == LockClock::large ? 96 : 48;
        if (design.clock != LockClock::none)
            labels += std::format(R"(
label {{
    monitor =
    text = $TIME
    color = {}
    font_size = {}
    position = 0, {}
    halign = center
    valign = center
}}
)",
                                  rgb(palette.text), clockSize, design.date ? (design.clock == LockClock::large ? 190 : 150) : 120);
        if (design.date)
            labels += std::format(R"(
label {{
    monitor =
    text = cmd[update:60000] date +"%A, %B %-d"
    color = {}
    font_size = 20
    position = 0, 80
    halign = center
    valign = center
}}
)",
                                  rgb(palette.subtle));

        return std::format(R"({} The {} theme; choosing another rewrites it.
{} background={} clock={} date={}

general {{
    hide_cursor = true
}}

background {{
    monitor =
{}    color = {}
}}
{}
input-field {{
    monitor =
    size = 320, 52
    outline_thickness = 2
    outer_color = {}
    inner_color = {}
    font_color = {}
    check_color = {}
    fail_color = {}
    placeholder_text = Password
    fade_on_empty = false
    position = 0, -40
    halign = center
    valign = center
}}
)",
                           lockMark, palette.name, designMark, backgroundName, clockName, design.date ? "yes" : "no", background, rgb(palette.base), labels,
                           rgb(palette.accent), rgb(palette.surface), rgb(palette.text), rgb(palette.audio), rgb(palette.alert));
    }

    bool lanternLockScreen(std::string_view config) {
        return config.starts_with(lockMark);
    }

    std::optional<LockDesign> lockDesignIn(std::string_view config) {
        if (!lanternLockScreen(config))
            return std::nullopt;
        LockDesign design;
        const auto at = config.find(designMark);
        if (at == std::string_view::npos)
            return design;
        const auto line  = config.substr(at, config.find('\n', at) - at);
        const auto value = [&line](std::string_view key) {
            const auto found = line.find(std::format(" {}=", key));
            if (found == std::string_view::npos)
                return std::string_view{};
            const auto begin = found + key.size() + 2;
            return line.substr(begin, line.find(' ', begin) - begin);
        };
        if (value("background") == "wallpaper")
            design.background = LockBackground::wallpaper;
        else if (value("background") == "color")
            design.background = LockBackground::color;
        if (value("clock") == "small")
            design.clock = LockClock::small;
        else if (value("clock") == "none")
            design.clock = LockClock::none;
        design.date = value("date") != "no";
        return design;
    }

    std::span<const std::string_view> lockKeys() {
        static constexpr std::array<std::string_view, 5> keys{"XF86ScreenSaver", "Scroll_Lock", "SUPER + Escape", "SUPER + SHIFT + L", "CTRL + ALT + L"};
        return keys;
    }

    std::string lockKeyIn(std::string_view config) {
        for (const auto& bind : configuration::luaBindLines(config))
            if (bind.command && *bind.command == lockNowRun)
                return bind.keys;
        return {};
    }

    std::string withLockKey(std::string_view config, std::string_view keys) {
        std::string text{config};
        for (const auto& bind : configuration::luaBindLines(config))
            if (bind.command == lockNowRun && !configuration::sameKeys(bind.keys, keys))
                text = configuration::withoutLuaBind(text, bind.keys);
        if (!keys.empty())
            text = configuration::withLuaExecBind(text, keys, lockNowRun);
        return text;
    }

    bool hasBacklight(const std::filesystem::path& classes) {
        std::error_code error;
        if (std::filesystem::directory_iterator{classes / "backlight", error} == std::filesystem::directory_iterator{})
            return false;
        for (const auto& entry : std::filesystem::directory_iterator{classes / "drm", error}) {
            const auto name = entry.path().filename().string();
            if (name.contains("-eDP-") || name.contains("-LVDS-") || name.contains("-DSI-"))
                return true;
        }
        return false;
    }

}
