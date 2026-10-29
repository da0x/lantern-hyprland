// SPDX-License-Identifier: GPL-3.0-or-later
#include "ipc/options.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace ipc {

    namespace {

        // A value from either reply, written as the config would write it. A list, such as
        // a vec2, is its items with spaces between.
        std::string textOf(const nlohmann::json& value) {
            if (value.is_boolean())
                return value.get<bool>() ? "true" : "false";
            if (value.is_number_integer())
                return std::to_string(value.get<long long>());
            if (value.is_number())
                return numberText(value.get<double>());
            if (value.is_string())
                return value.get<std::string>();
            if (value.is_array()) {
                std::string joined;
                for (const auto& item : value)
                    joined += (joined.empty() ? "" : " ") + textOf(item);
                return joined;
            }
            return {};
        }

        std::optional<double> numberOf(const nlohmann::json& object, const char* name) {
            const auto field = object.find(name);
            if (field == object.end() || !field->is_number())
                return std::nullopt;
            return field->get<double>();
        }

    }

    std::string numberText(double number) {
        if (std::isfinite(number) && number == std::round(number) && std::abs(number) < 1e15)
            return std::format("{}", static_cast<long long>(number));
        auto text = std::format("{:.6f}", number);
        while (text.ends_with('0'))
            text.pop_back();
        if (text.ends_with('.'))
            text.pop_back();
        return text;
    }

    std::expected<std::vector<OptionDescription>, std::string> parseOptionDescriptions(std::string_view reply) {
        const auto document = nlohmann::json::parse(reply, nullptr, false);
        if (document.is_discarded())
            return std::unexpected{"the options reply is not valid JSON"};
        if (!document.is_array())
            return std::unexpected{"the options reply is not a list"};

        std::vector<OptionDescription> options;
        options.reserve(document.size());
        for (const auto& object : document) {
            if (!object.is_object() || !object.contains("name") || !object["name"].is_string())
                continue;
            OptionDescription option{
                .name         = object["name"].get<std::string>(),
                .description  = object.value("description", std::string{}),
                .defaultValue = object.contains("default") ? textOf(object["default"]) : std::string{},
                .minimum      = numberOf(object, "min"),
                .maximum      = numberOf(object, "max"),
                .choices      = {},
            };
            // A list of one-entry objects: [{"bottom_left": 4}, {"disable": 0}].
            if (const auto map = object.find("map"); map != object.end() && map->is_array())
                for (const auto& entry : *map)
                    if (entry.is_object())
                        for (const auto& [choice, number] : entry.items())
                            if (number.is_number_integer())
                                option.choices.emplace_back(choice, number.get<long>());
            std::ranges::sort(option.choices, {}, &std::pair<std::string, long>::second);
            options.push_back(std::move(option));
        }
        return options;
    }

    std::vector<OptionValue> parseOptionValues(std::string_view reply, const std::vector<std::string>& names) {
        // Hyprland answers a batch with each reply in order, three newlines apart. A reply
        // that is not JSON, such as "invalid type (internal error)", still takes its place.
        std::vector<OptionValue> values;
        values.reserve(names.size());
        std::size_t start = 0;
        for (const auto& name : names) {
            const auto end   = reply.find("\n\n\n", start);
            const auto piece = reply.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
            start            = end == std::string_view::npos ? reply.size() : end + 3;

            OptionValue value{.name = name, .value = {}, .set = false};
            const auto  object = nlohmann::json::parse(piece, nullptr, false);
            if (object.is_object() && object.value("option", std::string{}) == name) {
                value.set = object.value("set", false);
                for (const auto& [key, field] : object.items())
                    if (key != "option" && key != "set")
                        value.value = textOf(field);
            }
            values.push_back(std::move(value));
        }
        return values;
    }

}
