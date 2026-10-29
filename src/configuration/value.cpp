// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/value.hpp"

#include <algorithm>
#include <cctype>
#include <format>

namespace configuration {

    namespace {

        bool isIdentifier(const std::string& text) {
            if (text.empty() || (std::isdigit(static_cast<unsigned char>(text.front())) != 0))
                return false;
            return std::ranges::all_of(text, [](char character) { return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_'; });
        }

        std::string quoted(const std::string& text) {
            std::string result = "\"";
            for (const char character : text) {
                switch (character) {
                    case '"': result += "\\\""; break;
                    case '\\': result += "\\\\"; break;
                    case '\n': result += "\\n"; break;
                    case '\t': result += "\\t"; break;
                    default: result += character;
                }
            }
            return result + "\"";
        }

        std::string arguments(const std::vector<Value>& values) {
            std::string text;
            for (const auto& value : values) {
                if (!text.empty())
                    text += ", ";
                text += value.lua();
            }
            return text;
        }

    }

    const Value* Value::field(std::string_view name) const {
        const auto* entries = table();
        if (entries == nullptr)
            return nullptr;
        for (const auto& [key, value] : *entries) {
            if (const auto* text = key.string(); text != nullptr && *text == name)
                return &value;
        }
        return nullptr;
    }

    std::string Value::lua() const {
        return std::visit(
            [](const auto& data) -> std::string {
                using Type = std::decay_t<decltype(data)>;
                if constexpr (std::is_same_v<Type, std::monostate>)
                    return "nil";
                else if constexpr (std::is_same_v<Type, bool>)
                    return data ? "true" : "false";
                else if constexpr (std::is_same_v<Type, std::int64_t> || std::is_same_v<Type, double>)
                    return std::format("{}", data);
                else if constexpr (std::is_same_v<Type, std::string>)
                    return quoted(data);
                else if constexpr (std::is_same_v<Type, Table>) {
                    if (data.empty())
                        return "{}";
                    std::string   text = "{ ";
                    std::int64_t  next = 1;
                    bool          first = true;
                    for (const auto& [key, value] : data) {
                        if (!first)
                            text += ", ";
                        first = false;
                        if (const auto* index = std::get_if<std::int64_t>(&key.data); index != nullptr && *index == next) {
                            ++next;
                        } else if (const auto* name = key.string(); name != nullptr && isIdentifier(*name)) {
                            text += *name + " = ";
                        } else {
                            text += "[" + key.lua() + "] = ";
                        }
                        text += value.lua();
                    }
                    return text + " }";
                } else if constexpr (std::is_same_v<Type, Function>)
                    return std::format("function ({})", data.definition.brief());
                else if constexpr (std::is_same_v<Type, Action>)
                    return std::format("{}({})", data.name, arguments(data.arguments));
                else
                    return std::format("<{}>", data.reason);
            },
            data);
    }

}
