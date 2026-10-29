// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/gsettings.hpp"

#include "desktop/process.hpp"
#include "desktop/system.hpp"

#include <charconv>
#include <cmath>
#include <fstream>

namespace desktop {

    namespace {

        // The file in a change's record that keeps the old values, a line each:
        // schema, key and value, apart by tabs.
        constexpr auto keptName = "gsettings";
        // Records from before there were other schemas left the schema out.
        constexpr auto oldSchema = "org.gnome.desktop.interface";

        std::optional<std::filesystem::path> gsettingsProgram(const foundation::Environment& environment) {
            return findProgram("gsettings", foundation::variable(environment, "PATH").value_or(""));
        }

        std::optional<double> number(std::string_view text) {
            double value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size())
                return std::nullopt;
            return value;
        }

        bool same(std::string_view one, std::string_view other) {
            if (one == other)
                return true;
            const auto first = number(one), second = number(other);
            return first && second && std::abs(*first - *second) < 1e-9;
        }

    }

    std::optional<std::string> gsetting(const std::string& schema, const std::string& key, const foundation::Environment& environment) {
        const auto program = gsettingsProgram(environment);
        if (!program)
            return std::nullopt;
        auto value = readCommand(*program, {"get", schema, key});
        if (!value)
            return std::nullopt;
        while (!value->empty() && (value->back() == '\n' || value->back() == ' '))
            value->pop_back();
        return *value;
    }

    std::vector<GSetting> gsettingChanges(std::vector<GSetting> wanted, const foundation::Environment& environment) {
        std::vector<GSetting> changes;
        for (auto& setting : wanted) {
            const auto current = gsetting(setting.schema, setting.key, environment);
            if (!current || same(*current, setting.wanted))
                continue;
            setting.current = *current;
            changes.push_back(std::move(setting));
        }
        return changes;
    }

    void setGsettings(const std::vector<GSetting>& settings, const std::filesystem::path& record, const foundation::Environment& environment) {
        const auto program = gsettingsProgram(environment);
        if (!program || settings.empty())
            return;
        std::ofstream kept{record / keptName};
        for (const auto& setting : settings) {
            kept << setting.schema << '\t' << setting.key << '\t' << setting.current << '\n';
            (void)readCommand(*program, {"set", setting.schema, setting.key, setting.wanted});
        }
    }

    void restoreGsettings(const std::filesystem::path& record, const foundation::Environment& environment) {
        const auto program = gsettingsProgram(environment);
        if (!program)
            return;
        std::ifstream kept{record / keptName};
        std::string   line;
        while (std::getline(kept, line)) {
            std::vector<std::string> fields;
            for (std::size_t at = 0;;) {
                const auto tab = line.find('\t', at);
                fields.push_back(line.substr(at, tab - at));
                if (tab == std::string::npos)
                    break;
                at = tab + 1;
            }
            if (fields.size() == 2)
                fields.insert(fields.begin(), oldSchema);
            if (fields.size() == 3)
                (void)readCommand(*program, {"set", fields[0], fields[1], fields[2]});
        }
    }

}
