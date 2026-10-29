// SPDX-License-Identifier: GPL-3.0-or-later
#include "lua/state.hpp"

#include <lua.hpp>

#include <cstdlib>
#include <cstring>
#include <exception>
#include <format>

namespace lua {

    struct State::Internals {
        lua_State*                                 state = nullptr;

        std::size_t                                used     = 0;
        bool                                       limiting = false;
        Limits                                     limits;
        std::uint64_t                              executed = 0;
        std::chrono::steady_clock::time_point      deadline;

        std::vector<std::unique_ptr<HostFunction>> functions;

        Internals()                            = default;
        Internals(const Internals&)            = delete;
        Internals& operator=(const Internals&) = delete;
        ~Internals() {
            if (state != nullptr)
                lua_close(state);
        }
    };

    namespace {

        // How many instructions run between two checks of the limits.
        constexpr int checkEvery = 10'000;

        // Deeper tables than this are cut off when copied out, which also stops a cycle.
        constexpr int deepest = 64;

        State::Internals& internalsOf(lua_State* state) {
            State::Internals* internals = nullptr;
            std::memcpy(&internals, lua_getextraspace(state), sizeof(internals));
            return *internals;
        }

        // Lua's allocator interface. Refusing an allocation makes Lua raise a memory error,
        // which is how the memory limit stops a run.
        void* allocate(void* data, void* block, std::size_t oldSize, std::size_t newSize) {
            auto&             internals = *static_cast<State::Internals*>(data);
            const std::size_t current   = block != nullptr ? oldSize : 0;

            if (newSize == 0) {
                std::free(block); // NOLINT(cppcoreguidelines-no-malloc): Lua's allocator contract
                internals.used -= current;
                return nullptr;
            }
            if (internals.limiting && newSize > current && internals.used - current + newSize > internals.limits.memory)
                return nullptr;

            void* resized = std::realloc(block, newSize); // NOLINT(cppcoreguidelines-no-malloc): Lua's allocator contract
            if (resized != nullptr)
                internals.used = internals.used - current + newSize;
            return resized;
        }

        // Runs every checkEvery instructions. luaL_error does not return: it jumps out of
        // here, so nothing in this function may own anything.
        void checkLimits(lua_State* state, lua_Debug*) {
            auto& internals = internalsOf(state);
            internals.executed += checkEvery;
            if (internals.executed > internals.limits.instructions)
                luaL_error(state, "stopped after %d million instructions", static_cast<int>(internals.limits.instructions / 1'000'000));
            if (std::chrono::steady_clock::now() > internals.deadline)
                luaL_error(state, "stopped after running for %d ms", static_cast<int>(internals.limits.time.count()));
        }

        Value copyOut(lua_State* state, int index, int depth) {
            index = lua_absindex(state, index);
            switch (lua_type(state, index)) {
                case LUA_TBOOLEAN: return Value{lua_toboolean(state, index) != 0};
                case LUA_TNUMBER:
                    if (lua_isinteger(state, index) != 0)
                        return Value{std::int64_t{lua_tointeger(state, index)}};
                    return Value{double{lua_tonumber(state, index)}};
                case LUA_TSTRING: {
                    std::size_t length = 0;
                    const char* text   = lua_tolstring(state, index, &length);
                    return Value{std::string{text, length}};
                }
                case LUA_TTABLE: {
                    if (depth >= deepest)
                        return {};
                    Table             entries;
                    const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(state, index));
                    for (lua_Integer position = 1; position <= count; ++position) {
                        lua_rawgeti(state, index, position);
                        entries.emplace_back(Value{std::int64_t{position}}, copyOut(state, -1, depth + 1));
                        lua_pop(state, 1);
                    }
                    lua_pushnil(state);
                    while (lua_next(state, index) != 0) {
                        const bool inArray = lua_isinteger(state, -2) != 0 && lua_tointeger(state, -2) >= 1 && lua_tointeger(state, -2) <= count;
                        if (!inArray)
                            entries.emplace_back(copyOut(state, -2, depth + 1), copyOut(state, -1, depth + 1));
                        lua_pop(state, 1);
                    }
                    return Value{std::move(entries)};
                }
                default: return {};
            }
        }

        void push(lua_State* state, const Value& value) {
            std::visit(
                [state](const auto& data) {
                    using Type = std::decay_t<decltype(data)>;
                    if constexpr (std::is_same_v<Type, std::monostate>)
                        lua_pushnil(state);
                    else if constexpr (std::is_same_v<Type, bool>)
                        lua_pushboolean(state, data ? 1 : 0);
                    else if constexpr (std::is_same_v<Type, std::int64_t>)
                        lua_pushinteger(state, data);
                    else if constexpr (std::is_same_v<Type, double>)
                        lua_pushnumber(state, data);
                    else if constexpr (std::is_same_v<Type, std::string>)
                        lua_pushlstring(state, data.data(), data.size());
                    else {
                        lua_createtable(state, 0, static_cast<int>(data.size()));
                        for (const auto& [key, entry] : data) {
                            push(state, key);
                            push(state, entry);
                            lua_rawset(state, -3);
                        }
                    }
                },
                value.data);
        }

        // The trampoline every host function is called through. Lua reports errors with
        // longjmp, which would skip C++ destructors, so all C++ work happens in the inner
        // scope and lua_error is called only after that scope has closed.
        int callHost(lua_State* state) {
            bool failed = false;
            {
                try {
                    const auto&        function = *static_cast<HostFunction*>(lua_touserdata(state, lua_upvalueindex(1)));
                    std::vector<Value> arguments;
                    for (int index = 1; index <= lua_gettop(state); ++index)
                        arguments.push_back(copyOut(state, index, 0));

                    const auto result = function(arguments);
                    if (result) {
                        push(state, *result);
                    } else {
                        lua_pushlstring(state, result.error().data(), result.error().size());
                        failed = true;
                    }
                } catch (const std::exception& error) {
                    lua_pushstring(state, error.what());
                    failed = true;
                }
            }
            return failed ? lua_error(state) : 1;
        }

    }

    State::State(std::unique_ptr<Internals> internals) : internals{std::move(internals)} {}
    State::State(State&&) noexcept            = default;
    State& State::operator=(State&&) noexcept = default;
    State::~State()                           = default;

    std::expected<State, std::string> State::create(const Limits& limits) {
        auto internals    = std::make_unique<Internals>();
        internals->limits = limits;

        // A fixed hash seed keeps the order of a table's entries the same from run to run.
        internals->state = lua_newstate(allocate, internals.get(), 0);
        if (internals->state == nullptr)
            return std::unexpected{"cannot create a Lua state"};

        Internals* address = internals.get();
        std::memcpy(lua_getextraspace(internals->state), &address, sizeof(address));

        // Everything except package, which can load C libraries. The trusted chunk decides
        // what the code it loads can reach.
        luaL_openselectedlibs(internals->state, ~LUA_LOADLIBK, 0);
        return State{std::move(internals)};
    }

    std::expected<Value, std::string> State::run(std::string_view source, std::string_view name, const std::map<std::string, HostFunction>& host) {
        lua_State* state = internals->state;
        lua_settop(state, 0);

        const std::string chunkName{name};
        if (luaL_loadbufferx(state, source.data(), source.size(), chunkName.c_str(), "t") != LUA_OK) {
            std::string error = lua_tostring(state, -1);
            lua_settop(state, 0);
            return std::unexpected{std::move(error)};
        }

        lua_createtable(state, 0, static_cast<int>(host.size()));
        for (const auto& [functionName, function] : host) {
            internals->functions.push_back(std::make_unique<HostFunction>(function));
            lua_pushlightuserdata(state, internals->functions.back().get());
            lua_pushcclosure(state, callHost, 1);
            lua_setfield(state, -2, functionName.c_str());
        }

        internals->executed = 0;
        internals->deadline = std::chrono::steady_clock::now() + internals->limits.time;
        internals->limiting = true;
        lua_sethook(state, checkLimits, LUA_MASKCOUNT, checkEvery);

        const int status = lua_pcall(state, 1, 1, 0);

        lua_sethook(state, nullptr, 0, 0);
        internals->limiting = false;

        // Copied out with the limits lifted, so the copy itself cannot run out of memory and
        // jump out of C++ code.
        std::expected<Value, std::string> result;
        if (status == LUA_OK) {
            result = copyOut(state, -1, 0);
        } else {
            const char* error = lua_tostring(state, -1);
            result            = std::unexpected{error != nullptr ? std::string{error} : std::string{"the chunk failed without a message"}};
        }
        lua_settop(state, 0);
        internals->functions.clear();
        lua_gc(state, LUA_GCCOLLECT);
        return result;
    }

}
