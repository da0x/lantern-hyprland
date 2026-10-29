// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/input.hpp"

#include "configuration/lua-edit.hpp"
#include "syntax/highlight.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <map>
#include <ranges>
#include <sstream>

namespace desktop {

    namespace {

        std::optional<std::string> luaValue(std::string_view config, std::string_view name, std::string_view table = {}) {
            const auto found = table.empty() ? syntax::luaCallField(config, "hl.config", {"input", name}) : syntax::luaCallField(config, "hl.config", {"input", table, name});
            if (!found)
                return std::nullopt;
            return std::string{config.substr(found->begin, found->end - found->begin)};
        }

        // "us,ara" without its quotes; nothing for a value that is not a string.
        std::optional<std::string> textOf(const std::optional<std::string>& value) {
            if (!value || value->size() < 2 || value->front() != '"' || value->back() != '"')
                return std::nullopt;
            return value->substr(1, value->size() - 2);
        }

        template <typename Number>
        std::optional<Number> numberOf(const std::optional<std::string>& value) {
            if (!value)
                return std::nullopt;
            Number     number{};
            const auto [end, error] = std::from_chars(value->data(), value->data() + value->size(), number);
            if (error != std::errc{} || end != value->data() + value->size())
                return std::nullopt;
            return number;
        }

        std::vector<std::string> split(std::string_view text) {
            std::vector<std::string> parts;
            std::size_t              start = 0;
            while (true) {
                const auto comma = text.find(',', start);
                parts.emplace_back(text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
                if (comma == std::string_view::npos)
                    return parts;
                start = comma + 1;
            }
        }

        std::string joined(const std::vector<std::string>& parts) {
            std::string text;
            for (std::size_t index = 0; index < parts.size(); ++index)
                text += (index == 0 ? "" : ",") + parts[index];
            return text;
        }

        bool capsOption(std::string_view option) {
            return option.starts_with("caps:") || option == "ctrl:nocaps" || option == "ctrl:swapcaps";
        }

        std::string layoutText(const InputSettings& settings) {
            std::vector<std::string> codes;
            for (const auto& layout : settings.layouts)
                codes.push_back(layout.code);
            return joined(codes);
        }

        std::string variantText(const InputSettings& settings) {
            std::vector<std::string> variants;
            for (const auto& layout : settings.layouts)
                variants.push_back(layout.variant);
            return std::ranges::all_of(variants, &std::string::empty) ? std::string{} : joined(variants);
        }

        std::string optionText(const InputSettings& settings) {
            std::vector<std::string> options;
            for (const auto* option : {&settings.switchKeys, &settings.capsLock})
                if (!option->empty())
                    options.push_back(*option);
            options.insert(options.end(), settings.otherOptions.begin(), settings.otherOptions.end());
            return options.empty() ? std::string{} : joined(options);
        }

        constexpr auto gestureCall = "hl.gesture";

        // The hl.gesture calls that switch workspaces with three fingers swiped sideways.
        std::vector<syntax::Range> workspaceSwipes(std::string_view config) {
            std::vector<syntax::Range> found;
            for (const auto& statement : syntax::luaStatements(config)) {
                if (statement.broken || statement.call != gestureCall)
                    continue;
                const auto text  = config.substr(statement.range.begin, statement.range.end - statement.range.begin);
                const auto field = [text](std::string_view name) {
                    const auto value = syntax::luaCallField(text, gestureCall, {name});
                    return value ? std::string{text.substr(value->begin, value->end - value->begin)} : std::string{};
                };
                if (field("fingers") == "3" && field("direction") == "\"horizontal\"" && field("action") == "\"workspace\"" && field("mods").empty())
                    found.push_back(statement.range);
            }
            return found;
        }

        std::string withWorkspaceSwipe(std::string_view config, bool on) {
            auto       text   = std::string{config};
            const auto swipes = workspaceSwipes(config);
            if (!on) {
                for (const auto& swipe : swipes | std::views::reverse) {
                    auto end = swipe.end;
                    if (end < text.size() && text[end] == '\n')
                        ++end;
                    // The blank line that set it apart goes with it.
                    if (end < text.size() && text[end] == '\n' && std::string_view{text}.substr(0, swipe.begin).ends_with("\n\n"))
                        ++end;
                    text.erase(swipe.begin, end - swipe.begin);
                }
                return text;
            }
            if (!swipes.empty())
                return text;
            constexpr std::string_view swipe = "hl.gesture({\n    fingers   = 3,\n    direction = \"horizontal\",\n    action    = \"workspace\",\n})\n";
            std::optional<std::size_t> after;
            for (const auto& statement : syntax::luaStatements(config))
                if (!statement.broken && statement.call == gestureCall)
                    after = statement.range.end;
            if (after) {
                text.insert(*after, "\n\n" + std::string{swipe.substr(0, swipe.size() - 1)});
                return text;
            }
            if (!text.empty() && !text.ends_with('\n'))
                text += '\n';
            return text + "\n" + std::string{swipe};
        }

        InputSettings inputIn(std::string_view config) {
            InputSettings settings;
            if (const auto codes = textOf(luaValue(config, "kb_layout")); codes && !codes->empty()) {
                const auto variants = split(textOf(luaValue(config, "kb_variant")).value_or(""));
                settings.layouts.clear();
                const auto parts = split(*codes);
                for (std::size_t index = 0; index < parts.size(); ++index)
                    settings.layouts.push_back(Layout{parts[index], index < variants.size() ? variants[index] : ""});
            }
            if (const auto options = textOf(luaValue(config, "kb_options")); options && !options->empty())
                for (auto& option : split(*options)) {
                    if (option.starts_with("grp:") && settings.switchKeys.empty())
                        settings.switchKeys = std::move(option);
                    else if (capsOption(option) && settings.capsLock.empty())
                        settings.capsLock = std::move(option);
                    else if (!option.empty())
                        settings.otherOptions.push_back(std::move(option));
                }
            settings.repeatRate      = numberOf<int>(luaValue(config, "repeat_rate")).value_or(settings.repeatRate);
            settings.repeatDelay     = numberOf<int>(luaValue(config, "repeat_delay")).value_or(settings.repeatDelay);
            settings.sensitivity     = numberOf<double>(luaValue(config, "sensitivity")).value_or(settings.sensitivity);
            settings.flat            = textOf(luaValue(config, "accel_profile")) == "flat";
            settings.leftHanded      = luaValue(config, "left_handed") == "true";
            settings.numLock         = luaValue(config, "numlock_by_default") == "true";
            settings.mouseNatural    = luaValue(config, "natural_scroll") == "true";
            settings.touchpadNatural = luaValue(config, "natural_scroll", "touchpad") == "true";
            settings.tapToClick      = luaValue(config, "tap_to_click", "touchpad") != "false";
            settings.typingDisables  = luaValue(config, "disable_while_typing", "touchpad") != "false";
            settings.twoFingerClick  = luaValue(config, "clickfinger_behavior", "touchpad") == "true";
            settings.touchpadScroll  = numberOf<double>(luaValue(config, "scroll_factor", "touchpad")).value_or(settings.touchpadScroll);
            settings.swipeWorkspaces = !workspaceSwipes(config).empty();
            return settings;
        }

        std::string inQuotes(std::string_view text) {
            return std::format("\"{}\"", text);
        }

    }

    std::optional<InputSettings> currentInput(const configuration::MainConfig& config) {
        if (!config.lua())
            return std::nullopt;
        std::ifstream stream{config.file, std::ios::binary};
        if (!stream)
            return std::nullopt;
        std::stringstream text;
        text << stream.rdbuf();
        return inputIn(text.str());
    }

    std::expected<std::optional<configuration::FileChange>, std::string> planInput(const InputSettings& wanted, const configuration::MainConfig& config) {
        if (!config.lua())
            return std::unexpected{"the keyboard and mouse are set only in a Lua config (hyprland.lua) so far"};
        if (wanted.layouts.empty())
            return std::unexpected{"a keyboard needs at least one layout"};
        return configuration::planEdit(config.file, [&wanted](std::string_view config) {
            const auto now  = inputIn(config);
            auto       text = std::string{config};
            const auto set  = [&text](std::string_view name, const std::string& value, bool differs) {
                if (differs)
                    text = configuration::withLuaConfigValueSet(text, {"input", name}, value);
            };
            set("kb_layout", inQuotes(layoutText(wanted)), layoutText(now) != layoutText(wanted));
            set("kb_variant", inQuotes(variantText(wanted)), variantText(now) != variantText(wanted));
            set("kb_options", inQuotes(optionText(wanted)), optionText(now) != optionText(wanted));
            set("repeat_rate", std::to_string(wanted.repeatRate), now.repeatRate != wanted.repeatRate);
            set("repeat_delay", std::to_string(wanted.repeatDelay), now.repeatDelay != wanted.repeatDelay);
            set("sensitivity", std::format("{:g}", wanted.sensitivity), now.sensitivity != wanted.sensitivity);
            set("accel_profile", inQuotes(wanted.flat ? "flat" : "adaptive"), now.flat != wanted.flat);
            set("left_handed", wanted.leftHanded ? "true" : "false", now.leftHanded != wanted.leftHanded);
            set("numlock_by_default", wanted.numLock ? "true" : "false", now.numLock != wanted.numLock);
            set("natural_scroll", wanted.mouseNatural ? "true" : "false", now.mouseNatural != wanted.mouseNatural);
            const auto touchpad = [&text](std::string_view name, const std::string& value, bool differs) {
                if (differs)
                    text = configuration::withLuaConfigValueSet(text, {"input", "touchpad", name}, value);
            };
            touchpad("natural_scroll", wanted.touchpadNatural ? "true" : "false", now.touchpadNatural != wanted.touchpadNatural);
            touchpad("tap_to_click", wanted.tapToClick ? "true" : "false", now.tapToClick != wanted.tapToClick);
            touchpad("disable_while_typing", wanted.typingDisables ? "true" : "false", now.typingDisables != wanted.typingDisables);
            touchpad("clickfinger_behavior", wanted.twoFingerClick ? "true" : "false", now.twoFingerClick != wanted.twoFingerClick);
            touchpad("scroll_factor", std::format("{:g}", wanted.touchpadScroll), now.touchpadScroll != wanted.touchpadScroll);
            if (now.swipeWorkspaces != wanted.swipeWorkspaces)
                text = withWorkspaceSwipe(text, wanted.swipeWorkspaces);
            return text;
        });
    }

    std::vector<KnownLayout> parseLayouts(std::string_view list) {
        std::vector<KnownLayout>                        layouts;
        std::map<std::string, std::vector<KnownLayout>> variants;
        std::string_view                                section;
        std::size_t                                     start = 0;
        while (start < list.size()) {
            const auto end  = list.find('\n', start);
            auto       line = list.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
            start           = end == std::string_view::npos ? list.size() : end + 1;
            if (line.starts_with("! ")) {
                section = line.substr(2);
                continue;
            }
            const auto codeStart = line.find_first_not_of(' ');
            if (codeStart == std::string_view::npos)
                continue;
            const auto codeEnd  = line.find(' ', codeStart);
            const auto nameFrom = codeEnd == std::string_view::npos ? std::string_view::npos : line.find_first_not_of(' ', codeEnd);
            if (nameFrom == std::string_view::npos)
                continue;
            const auto code = std::string{line.substr(codeStart, codeEnd - codeStart)};
            const auto name = line.substr(nameFrom);
            if (section == "layout")
                layouts.push_back(KnownLayout{code, std::string{name}, {}});
            else if (section == "variant")
                // "ara: Arabic (AZERTY)": the layout it belongs to, then its name.
                if (const auto colon = name.find(": "); colon != std::string_view::npos)
                    variants[std::string{name.substr(0, colon)}].push_back(KnownLayout{code, std::string{name.substr(colon + 2)}, {}});
        }
        for (auto& layout : layouts)
            if (const auto found = variants.find(layout.code); found != variants.end())
                layout.variants = std::move(found->second);
        std::ranges::sort(layouts, {}, &KnownLayout::name);
        return layouts;
    }

    bool touchpadConnected(const std::filesystem::path& data) {
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator{data, error}) {
            // Input devices are character devices with the major number 13.
            if (!entry.path().filename().string().starts_with("c13:"))
                continue;
            std::ifstream stream{entry.path()};
            for (std::string line; std::getline(stream, line);)
                if (line == "E:ID_INPUT_TOUCHPAD=1")
                    return true;
        }
        return false;
    }

    std::vector<KnownLayout> installedLayouts(const std::filesystem::path& list) {
        std::ifstream stream{list, std::ios::binary};
        if (!stream)
            return {};
        std::stringstream text;
        text << stream.rdbuf();
        return parseLayouts(text.str());
    }

}
