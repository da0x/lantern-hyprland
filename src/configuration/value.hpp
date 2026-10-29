// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "configuration/location.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace configuration {

    struct Value;

    // A table's entries, the array part first.
    using Table = std::vector<std::pair<Value, Value>>;

    // A function in the config. Lantern never runs it; it can only say where it is.
    struct Function {
        Location definition;

        friend bool operator==(const Function&, const Function&) = default;
    };

    // What an hl.dsp.* call returned: the dispatcher's name and its arguments.
    struct Action {
        std::string        name;
        std::vector<Value> arguments;

        friend bool operator==(const Action&, const Action&) = default;
    };

    // A value the config could not hand over as data, and why.
    struct Unreadable {
        std::string reason;

        friend bool operator==(const Unreadable&, const Unreadable&) = default;
    };

    // An argument as the config passed it.
    struct Value {
        std::variant<std::monostate, bool, std::int64_t, double, std::string, Table, Function, Action, Unreadable> data;

        [[nodiscard]] const std::string* string() const { return std::get_if<std::string>(&data); }
        [[nodiscard]] const Table*       table() const { return std::get_if<Table>(&data); }
        [[nodiscard]] const Action*      action() const { return std::get_if<Action>(&data); }
        [[nodiscard]] const Function*    function() const { return std::get_if<Function>(&data); }

        // The value under a string key, or nullptr.
        [[nodiscard]] const Value* field(std::string_view name) const;

        // The value written back as Lua: exec_cmd("kitty"), { direction = "left" }.
        [[nodiscard]] std::string lua() const;

        friend bool               operator==(const Value&, const Value&) = default;
    };

}
