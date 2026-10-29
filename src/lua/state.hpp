// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lua/value.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lua {

    // What one run may use before it is stopped.
    struct Limits {
        std::size_t               memory       = std::size_t{256} << 20U;
        std::uint64_t             instructions = 200'000'000;
        std::chrono::milliseconds time{1500};
    };

    // A function the host offers to a chunk. It takes and returns plain values, and its
    // error becomes a Lua error.
    using HostFunction = std::function<std::expected<Value, std::string>(const std::vector<Value>& arguments)>;

    // A Lua 5.5 state with the standard libraries open. Only trusted code runs directly in
    // it: Lantern's own chunks decide what anything they load may reach.
    class State {
      public:
        [[nodiscard]] static std::expected<State, std::string> create(const Limits& limits);

        State(State&&) noexcept;
        State& operator=(State&&) noexcept;
        ~State();

        // Runs a trusted chunk. It receives one argument, a table of the host functions,
        // and its first return value is copied out as plain data (functions and other
        // values that cannot be copied become nil). The limits apply only while it runs.
        [[nodiscard]] std::expected<Value, std::string> run(std::string_view source, std::string_view name, const std::map<std::string, HostFunction>& host);

        struct Internals;

      private:
        explicit State(std::unique_ptr<Internals> internals);

        std::unique_ptr<Internals> internals;
    };

}
