// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/devices.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <system_error>

namespace desktop {

    namespace {

        std::vector<std::string_view> linesOf(std::string_view text) {
            std::vector<std::string_view> lines;
            std::size_t                   start = 0;
            while (start < text.size()) {
                const auto end = text.find('\n', start);
                lines.push_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
                start = end == std::string_view::npos ? text.size() : end + 1;
            }
            return lines;
        }

        // nmcli's terse fields: split at colons, with "\:" a colon inside a field.
        std::vector<std::string> fieldsOf(std::string_view line) {
            std::vector<std::string> fields(1);
            for (std::size_t at = 0; at < line.size(); ++at) {
                if (line[at] == '\\' && at + 1 < line.size())
                    fields.back() += line[++at];
                else if (line[at] == ':')
                    fields.emplace_back();
                else
                    fields.back() += line[at];
            }
            return fields;
        }

        // `key = "value"` from wpctl inspect's lines, which start with "  * " or "    ".
        std::optional<std::string> quotedValue(std::string_view output, std::string_view key) {
            for (auto line : linesOf(output)) {
                const auto at = line.find(key);
                if (at == std::string_view::npos || line.substr(at + key.size()).find_first_not_of(' ') == std::string_view::npos)
                    continue;
                auto rest = line.substr(at + key.size());
                rest      = rest.substr(rest.find_first_not_of(' '));
                if (!rest.starts_with("= \"") || !rest.ends_with('"'))
                    continue;
                return std::string{rest.substr(3, rest.size() - 4)};
            }
            return std::nullopt;
        }

    }

    std::optional<Volume> parseVolume(std::string_view output) {
        constexpr std::string_view lead = "Volume: ";
        const auto                 at   = output.find(lead);
        if (at == std::string_view::npos)
            return std::nullopt;
        const auto number = output.substr(at + lead.size());
        double     value  = 0;
        if (std::from_chars(number.data(), number.data() + number.size(), value).ec != std::errc{})
            return std::nullopt;
        return Volume{.percent = static_cast<int>(std::lround(value * 100)), .muted = output.contains("[MUTED]")};
    }

    std::string parseNodeName(std::string_view output) {
        return quotedValue(output, "node.nick").or_else([output] { return quotedValue(output, "node.description"); }).value_or("");
    }

    std::vector<Connection> parseConnections(std::string_view output) {
        std::vector<Connection> connections;
        for (const auto line : linesOf(output)) {
            const auto fields = fieldsOf(line);
            if (fields.size() != 3 || fields[1] == "loopback" || fields[1] == "bridge")
                continue;
            const auto kind = fields[1] == "802-3-ethernet" ? std::string{"wired"} : fields[1] == "802-11-wireless" ? std::string{"Wi-Fi"} : fields[1];
            connections.push_back(Connection{fields[0], kind, fields[2]});
        }
        return connections;
    }

    std::vector<std::string> bluetoothAdapters(const std::filesystem::path& folder) {
        std::vector<std::string> adapters;
        std::error_code          error;
        for (const auto& entry : std::filesystem::directory_iterator{folder, error})
            if (const auto name = entry.path().filename().string(); name.starts_with("hci") && !name.contains(':'))
                adapters.push_back(name);
        std::ranges::sort(adapters);
        return adapters;
    }

}
