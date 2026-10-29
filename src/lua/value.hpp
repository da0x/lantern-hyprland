// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace lua {

    struct Value;

    // A table's entries, the array part first and in order.
    using Table = std::vector<std::pair<Value, Value>>;

    // Plain Lua data, copied out of a state so nothing refers back into it.
    struct Value {
        std::variant<std::monostate, bool, std::int64_t, double, std::string, Table> data;

        [[nodiscard]] bool               isNil() const { return std::holds_alternative<std::monostate>(data); }
        [[nodiscard]] const std::string* string() const { return std::get_if<std::string>(&data); }
        [[nodiscard]] const Table*       table() const { return std::get_if<Table>(&data); }
        [[nodiscard]] const std::int64_t* integer() const { return std::get_if<std::int64_t>(&data); }

        // The value under a string key, or nullptr.
        [[nodiscard]] const Value* field(std::string_view name) const;

        // The values under 1, 2, 3 and so on, in order.
        [[nodiscard]] std::vector<const Value*> array() const;
    };

}
