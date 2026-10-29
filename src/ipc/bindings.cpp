// SPDX-License-Identifier: GPL-3.0-or-later
#include "ipc/bindings.hpp"

#include <nlohmann/json.hpp>

namespace ipc {

    namespace {

        // A field of the wrong type reads as missing, because Hyprland has changed field types
        // before (submap_universal is still a string; see docs/upstream.md).
        std::string text(const nlohmann::json& object, const char* name) {
            const auto field = object.find(name);
            return field != object.end() && field->is_string() ? field->get<std::string>() : std::string{};
        }

        bool flag(const nlohmann::json& object, const char* name) {
            const auto field = object.find(name);
            return field != object.end() && field->is_boolean() && field->get<bool>();
        }

    }

    std::expected<std::vector<Binding>, std::string> parseBindings(std::string_view reply) {
        const auto document = nlohmann::json::parse(reply, nullptr, false);
        if (document.is_discarded())
            return std::unexpected{"the bindings reply is not valid JSON"};
        if (!document.is_array())
            return std::unexpected{"the bindings reply is not a list"};

        std::vector<Binding> bindings;
        bindings.reserve(document.size());
        for (const auto& object : document) {
            if (!object.is_object())
                return std::unexpected{"a binding in the reply is not an object"};

            const auto mask = object.find("modmask");
            bindings.push_back(Binding{
                .modifiers    = foundation::Modifiers{mask != object.end() && mask->is_number_unsigned() ? mask->get<std::uint32_t>() : 0U},
                .key          = text(object, "key"),
                .submap       = text(object, "submap"),
                .dispatcher   = text(object, "dispatcher"),
                .argument     = text(object, "arg"),
                .description  = text(object, "description"),
                .mouse        = flag(object, "mouse"),
                .release      = flag(object, "release"),
                .repeat       = flag(object, "repeat"),
                .locked       = flag(object, "locked"),
                .longPress    = flag(object, "longPress"),
                .nonConsuming = flag(object, "non_consuming"),
            });
        }
        return bindings;
    }

}
