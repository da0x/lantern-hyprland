// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/recording.hpp"

#include "lua/state.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <string_view>

namespace configuration {

    namespace {

        constexpr char recorderSource[] = {
#embed "recorder.lua"
            , '\0'};

        std::filesystem::path homeFolder() {
            const char* home = std::getenv("HOME");
            return home != nullptr ? std::filesystem::path{home} : std::filesystem::path{};
        }

        // The rule for every file the config may read: inside its folder, written the way
        // the config wrote it rather than where links lead, and a regular file once they
        // are followed.
        std::expected<lua::Value, std::string> resolve(const std::filesystem::path& folder, const std::vector<lua::Value>& arguments) {
            const auto* text = arguments.empty() ? nullptr : arguments.front().string();
            if (text == nullptr)
                return std::unexpected{"expected a path"};

            std::filesystem::path path{*text};
            if (text->starts_with("~/"))
                path = homeFolder() / text->substr(2);
            else if (path.is_relative())
                path = folder / path;
            path = path.lexically_normal();

            const auto [inFolder, inPath] = std::ranges::mismatch(folder, path);
            if (inFolder != folder.end() && !inFolder->empty())
                return std::unexpected{std::format("{} is outside the config folder, which is all Lantern reads", path.string())};

            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error))
                return std::unexpected{std::format("{} is not a file", path.string())};

            return lua::Value{path.string()};
        }

        std::string text(const lua::Value* value) {
            const auto* string = value != nullptr ? value->string() : nullptr;
            return string != nullptr ? *string : std::string{};
        }

        std::vector<Location> stack(const lua::Value* frames) {
            std::vector<Location> locations;
            if (frames == nullptr)
                return locations;
            for (const auto* frame : frames->array()) {
                const auto* line = frame->field("line") != nullptr ? frame->field("line")->integer() : nullptr;
                locations.push_back(Location{.file = text(frame->field("file")), .line = line != nullptr ? static_cast<int>(*line) : 0});
            }
            return locations;
        }

        // Undoes recorder.lua's tagging: every value arrives as { t = kind, ... }.
        Value decode(const lua::Value& tagged) {
            const auto kind  = text(tagged.field("t"));
            const auto* data = tagged.field("v");

            if (kind == "boolean" && data != nullptr)
                if (const auto* flag = std::get_if<bool>(&data->data))
                    return Value{*flag};
            if (kind == "integer" && data != nullptr)
                if (const auto* number = data->integer())
                    return Value{*number};
            if (kind == "number" && data != nullptr)
                if (const auto* number = std::get_if<double>(&data->data))
                    return Value{*number};
            if (kind == "string")
                return Value{text(data)};
            if (kind == "function") {
                const auto* line = tagged.field("line") != nullptr ? tagged.field("line")->integer() : nullptr;
                return Value{Function{.definition = Location{.file = text(tagged.field("file")), .line = line != nullptr ? static_cast<int>(*line) : 0}}};
            }
            if (kind == "action") {
                Action action{.name = text(tagged.field("name")), .arguments = {}};
                if (const auto* arguments = tagged.field("args"))
                    for (const auto* argument : arguments->array())
                        action.arguments.push_back(decode(*argument));
                return Value{std::move(action)};
            }
            if (kind == "table") {
                Table table;
                if (const auto* entries = tagged.field("entries"))
                    for (const auto* entry : entries->array()) {
                        const auto* key   = entry->field("k");
                        const auto* value = entry->field("v");
                        if (key != nullptr && value != nullptr)
                            table.emplace_back(decode(*key), decode(*value));
                    }
                return Value{std::move(table)};
            }
            if (kind == "unreadable")
                return Value{Unreadable{.reason = text(data)}};
            return {};
        }

    }

    std::expected<Recording, std::string> record(const std::filesystem::path& mainFile) {
        auto state = lua::State::create(lua::Limits{});
        if (!state)
            return std::unexpected{state.error()};

        const auto main   = std::filesystem::absolute(mainFile).lexically_normal();
        const auto folder = main.parent_path();

        const std::map<std::string, lua::HostFunction> host{
            {"main", [&main](const auto&) -> std::expected<lua::Value, std::string> { return lua::Value{main.string()}; }},
            {"folder", [&folder](const auto&) -> std::expected<lua::Value, std::string> { return lua::Value{folder.string()}; }},
            {"resolve", [&folder](const auto& arguments) { return resolve(folder, arguments); }},
        };

        const auto result = state->run(std::string_view{recorderSource}, "=recorder", host);
        if (!result)
            return std::unexpected{std::format("the recording interpreter failed: {}", result.error())};

        Recording recording;
        if (const auto* calls = result->field("calls"))
            for (const auto* call : calls->array()) {
                Call decoded{.function = text(call->field("name")), .arguments = {}, .submap = text(call->field("submap")), .stack = stack(call->field("stack"))};
                if (const auto* arguments = call->field("args"))
                    for (const auto* argument : arguments->array())
                        decoded.arguments.push_back(decode(*argument));
                recording.calls.push_back(std::move(decoded));
            }
        if (const auto* refused = result->field("blocked"))
            for (const auto* refusal : refused->array())
                recording.refused.push_back(Refusal{.what = text(refusal->field("what")), .stack = stack(refusal->field("stack"))});
        if (const auto* files = result->field("files"))
            for (const auto* file : files->array())
                recording.files.emplace_back(text(file));
        if (const auto* failure = result->field("error"); failure != nullptr && !failure->isNil())
            recording.failure = Failure{.message = text(failure->field("message")), .stack = stack(failure->field("stack"))};
        return recording;
    }

}
