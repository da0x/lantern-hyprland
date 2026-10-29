// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/options.hpp"

#include <algorithm>
#include <set>

namespace application {

    namespace {

        // Walks one table of an hl.config call, `prefix` being the names above it. Hyprland
        // names a nested option with ":", and some with ".", as in "general:col.active_border",
        // so both are tried.
        void walk(const configuration::Table& table, const std::string& prefix, const std::set<std::string>& names, const configuration::Location& location,
                  std::map<std::string, configuration::Location>& found) {
            for (const auto& [key, value] : table) {
                const auto* text = key.string();
                if (!text)
                    continue;
                for (const auto* separator : {":", "."}) {
                    if (prefix.empty() && *separator == '.')
                        continue;
                    const auto name = prefix.empty() ? *text : prefix + separator + *text;
                    if (names.contains(name))
                        found[name] = location;
                    else if (const auto* inner = value.table(); inner && names.lower_bound(name) != names.end() && names.lower_bound(name)->starts_with(name))
                        walk(*inner, name, names, location, found);
                }
            }
        }

    }

    std::map<std::string, configuration::Location> optionLocations(const configuration::Recording& recording, const std::vector<std::string>& names) {
        const std::set<std::string>                    known{names.begin(), names.end()};
        std::map<std::string, configuration::Location> found;
        for (const auto& call : recording.calls)
            if (call.function == "config" && !call.arguments.empty())
                if (const auto* table = call.arguments.front().table())
                    walk(*table, {}, known, call.location(), found);
        return found;
    }

    Options joinOptions(std::vector<ipc::OptionDescription> descriptions, const std::vector<ipc::OptionValue>& values,
                        const std::expected<configuration::Recording, std::string>& recording) {
        std::vector<std::string> names;
        for (const auto& description : descriptions)
            names.push_back(description.name);
        const auto locations = recording ? optionLocations(*recording, names) : std::map<std::string, configuration::Location>{};

        Options options{.rows = {}, .configurationProblem = recording ? std::string{} : recording.error()};
        if (recording && recording->failure)
            options.configurationProblem = recording->failure->message;
        for (auto& description : descriptions) {
            OptionRow row{.description = std::move(description), .value = {}, .set = false, .where = std::nullopt};
            if (const auto value = std::ranges::find(values, row.description.name, &ipc::OptionValue::name); value != values.end()) {
                row.value = value->value;
                row.set   = value->set;
            }
            if (const auto where = locations.find(row.description.name); where != locations.end())
                row.where = where->second;
            options.rows.push_back(std::move(row));
        }
        return options;
    }

}
