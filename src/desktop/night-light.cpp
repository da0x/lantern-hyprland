// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/night-light.hpp"

#include "configuration/hyprlang-edit.hpp"

#include <charconv>
#include <format>
#include <ranges>

namespace desktop {

    namespace {

        std::string valueOf(const configuration::HyprlangBlock& block, std::string_view key) {
            for (const auto& [name, value] : block.values)
                if (name == key)
                    return value;
            return {};
        }

        // "7:30" as "07:30"; nothing for what is not a time.
        std::optional<std::string> timeOf(std::string_view text) {
            const auto colon   = text.find(':');
            int        hours   = 0;
            int        minutes = 0;
            if (colon == std::string_view::npos || std::from_chars(text.data(), text.data() + colon, hours).ec != std::errc{} ||
                std::from_chars(text.data() + colon + 1, text.data() + text.size(), minutes).ec != std::errc{} || hours > 23 || minutes > 59)
                return std::nullopt;
            return std::format("{:02}:{:02}", hours, minutes);
        }

    }

    std::filesystem::path hyprsunsetConfigFile(const foundation::Environment& environment) {
        if (const auto home = foundation::variable(environment, "XDG_CONFIG_HOME"); home && std::filesystem::path{*home}.is_absolute())
            return std::filesystem::path{*home} / "hypr" / "hyprsunset.conf";
        return std::filesystem::path{foundation::variable(environment, "HOME").value_or("")} / ".config" / "hypr" / "hyprsunset.conf";
    }

    std::optional<NightLight> nightLightIn(std::string_view config) {
        std::optional<std::string> evening;
        std::optional<std::string> morning;
        int                        temperature = 0;
        for (const auto& block : configuration::hyprlangBlocks(config)) {
            if (block.name != "profile")
                continue;
            const auto time = timeOf(valueOf(block, "time"));
            if (!time)
                continue;
            if (valueOf(block, "identity") == "true")
                morning = time;
            else if (const auto kelvin = valueOf(block, "temperature"); !kelvin.empty() && std::from_chars(kelvin.data(), kelvin.data() + kelvin.size(), temperature).ec == std::errc{})
                evening = time;
        }
        if (!evening || !morning)
            return std::nullopt;
        return NightLight{.temperature = temperature, .evening = *evening, .morning = *morning};
    }

    std::string withNightLight(std::string_view config, const NightLight& light) {
        std::string text{config};
        for (const auto& block : configuration::hyprlangBlocks(config) | std::views::reverse) {
            if (block.name != "profile")
                continue;
            auto begin = block.begin;
            // The blank line that set it apart goes with it.
            if (begin >= 2 && text[begin - 1] == '\n' && text[begin - 2] == '\n')
                --begin;
            text.erase(begin, block.end - begin);
        }
        if (!text.empty() && !text.ends_with('\n'))
            text += '\n';
        if (!text.empty())
            text += '\n';
        return text + std::format("profile {{\n    time = {}\n    identity = true\n}}\n\nprofile {{\n    time = {}\n    temperature = {}\n}}\n", light.morning, light.evening,
                                  light.temperature);
    }

}
