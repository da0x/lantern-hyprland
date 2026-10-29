// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/bindings.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <format>
#include <ranges>

namespace configuration {

    namespace {

        using Modifier = foundation::Modifiers::Modifier;

        constexpr std::array modifierNames{
            std::pair{std::string_view{"SHIFT"}, Modifier::shift}, std::pair{std::string_view{"CAPS"}, Modifier::caps},
            std::pair{std::string_view{"CTRL"}, Modifier::control}, std::pair{std::string_view{"CONTROL"}, Modifier::control},
            std::pair{std::string_view{"ALT"}, Modifier::alt},      std::pair{std::string_view{"MOD1"}, Modifier::alt},
            std::pair{std::string_view{"MOD2"}, Modifier::mod2},    std::pair{std::string_view{"MOD3"}, Modifier::mod3},
            std::pair{std::string_view{"SUPER"}, Modifier::super}, std::pair{std::string_view{"WIN"}, Modifier::super},
            std::pair{std::string_view{"LOGO"}, Modifier::super},  std::pair{std::string_view{"MOD4"}, Modifier::super},
            std::pair{std::string_view{"MOD5"}, Modifier::mod5},
        };

        std::string_view trimmed(std::string_view text) {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
                text.remove_prefix(1);
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
                text.remove_suffix(1);
            return text;
        }

        std::string upper(std::string_view text) {
            std::string result{text};
            std::ranges::transform(result, result.begin(), [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
            return result;
        }

        bool flag(const Value* options, std::string_view name) {
            const auto* value = options != nullptr ? options->field(name) : nullptr;
            const auto* set   = value != nullptr ? std::get_if<bool>(&value->data) : nullptr;
            return set != nullptr && *set;
        }

    }

    std::expected<std::pair<foundation::Modifiers, std::string>, std::string> parseKeys(std::string_view keys) {
        std::vector<std::string_view> parts;
        for (const auto part : keys | std::views::split('+'))
            parts.push_back(trimmed(std::string_view{part.begin(), part.end()}));

        // "SUPER + +" binds the plus key: an empty last part after a "+" is the key itself.
        if (parts.size() >= 2 && parts.back().empty() && parts[parts.size() - 2].empty()) {
            parts.pop_back();
            parts.back() = "+";
        }

        const auto key = parts.empty() ? std::string_view{} : parts.back();
        if (key.empty())
            return std::unexpected{std::format("no key in \"{}\"", keys)};

        std::uint32_t mask = 0;
        for (const auto part : parts | std::views::take(parts.size() - 1)) {
            const auto name  = upper(part);
            const auto found = std::ranges::find(modifierNames, std::string_view{name}, &decltype(modifierNames)::value_type::first);
            if (found == modifierNames.end())
                return std::unexpected{std::format("\"{}\" in \"{}\" is not a modifier", part, keys)};
            mask |= static_cast<std::uint32_t>(found->second);
        }
        return std::pair{foundation::Modifiers{mask}, std::string{key}};
    }

    Bindings bindings(const Recording& recording) {
        Bindings result;
        for (const auto& call : recording.calls) {
            if (call.function != "bind")
                continue;

            const auto* keys = call.arguments.empty() ? nullptr : call.arguments.front().string();
            if (keys == nullptr) {
                result.unreadable.push_back(std::format("{}: the keys are not a string", call.location().brief()));
                continue;
            }
            auto parsed = parseKeys(*keys);
            if (!parsed) {
                result.unreadable.push_back(std::format("{}: {}", call.location().brief(), parsed.error()));
                continue;
            }

            const Value* options     = call.arguments.size() >= 3 ? &call.arguments[2] : nullptr;
            const bool   mouse       = flag(options, "mouse") || parsed->second.starts_with("mouse");
            std::string  description;
            for (const auto name : {"description", "desc"})
                if (const auto* value = options != nullptr ? options->field(name) : nullptr; value != nullptr && value->string() != nullptr && description.empty())
                    description = *value->string();

            result.bindings.push_back(Binding{
                .modifiers    = parsed->first,
                .key          = std::move(parsed->second),
                .submap       = call.submap,
                .action       = call.arguments.size() >= 2 ? call.arguments[1] : Value{},
                .description  = std::move(description),
                .location     = call.location(),
                .mouse        = mouse,
                .release      = flag(options, "release"),
                .repeat       = flag(options, "repeating"),
                .locked       = flag(options, "locked"),
                .longPress    = flag(options, "long_press"),
                .nonConsuming = flag(options, "non_consuming"),
            });
        }
        return result;
    }

}
