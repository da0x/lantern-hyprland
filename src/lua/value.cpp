// SPDX-License-Identifier: GPL-3.0-or-later
#include "lua/value.hpp"

namespace lua {

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

    std::vector<const Value*> Value::array() const {
        std::vector<const Value*> values;
        const auto*               entries = table();
        if (entries == nullptr)
            return values;
        for (const auto& [key, value] : *entries) {
            const auto* index = key.integer();
            if (index == nullptr || *index != static_cast<std::int64_t>(values.size()) + 1)
                break;
            values.push_back(&value);
        }
        return values;
    }

}
