// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/plugins.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>

namespace application {

    namespace {

        // Letters and digits only, in lower case, so "Hypr-3D" and "hypr3d" are one name.
        std::string plain(std::string_view text) {
            std::string result;
            for (const char c : text)
                if (std::isalnum(static_cast<unsigned char>(c)))
                    result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            return result;
        }

        // The plugin name a file stands for: hypr3d for libhypr3d.so and hypr3d.so.1.
        std::string nameOfFile(const std::filesystem::path& file) {
            std::string_view name = file.filename().native();
            name                  = name.substr(0, name.find(".so"));
            if (name.starts_with("lib"))
                name.remove_prefix(3);
            return plain(name);
        }

        bool contains(const std::vector<std::filesystem::path>& files, const std::filesystem::path& file) {
            return std::ranges::find(files, file) != files.end();
        }

        std::filesystem::path expanded(std::string_view text, const std::filesystem::path& home) {
            if (text.starts_with("~/"))
                return home / text.substr(2);
            return std::filesystem::path{text};
        }

        std::string_view trimmed(std::string_view text) {
            const auto first = text.find_first_not_of(" \t\r");
            if (first == std::string_view::npos)
                return {};
            const auto last = text.find_last_not_of(" \t\r");
            return text.substr(first, last - first + 1);
        }

    }

    std::vector<Plugin> listPlugins(const std::vector<ipc::LoadedPlugin>& loaded, const std::vector<std::filesystem::path>& mapped,
                                    const std::vector<std::filesystem::path>& configured, const std::vector<std::filesystem::path>& remembered) {
        std::vector<Plugin> plugins;
        for (const auto& each : loaded) {
            Plugin plugin;
            plugin.name        = each.name;
            plugin.author      = each.author;
            plugin.version     = each.version;
            plugin.description = each.description;
            plugin.loaded      = true;
            std::optional<std::filesystem::path> guess;
            for (const auto& file : mapped) {
                if (nameOfFile(file) != plain(each.name))
                    continue;
                if (contains(configured, file) || contains(remembered, file)) {
                    guess = file;
                    break;
                }
                if (!guess)
                    guess = file;
            }
            plugin.file       = guess;
            plugin.inConfig   = guess && contains(configured, *guess);
            plugin.remembered = guess && contains(remembered, *guess);
            plugins.push_back(std::move(plugin));
        }

        const auto listed = [&plugins](const std::filesystem::path& file) { return std::ranges::any_of(plugins, [&file](const Plugin& plugin) { return plugin.file == file; }); };
        const auto notLoaded = [](const std::filesystem::path& file) {
            Plugin plugin;
            plugin.name = nameOfFile(file);
            plugin.file = file;
            return plugin;
        };
        for (const auto& file : configured) {
            if (listed(file))
                continue;
            auto plugin       = notLoaded(file);
            plugin.inConfig   = true;
            plugin.remembered = contains(remembered, file);
            plugins.push_back(std::move(plugin));
        }
        for (const auto& file : remembered) {
            std::error_code error;
            if (listed(file) || !std::filesystem::is_regular_file(file, error))
                continue;
            auto plugin       = notLoaded(file);
            plugin.remembered = true;
            plugins.push_back(std::move(plugin));
        }
        return plugins;
    }

    std::vector<std::filesystem::path> pluginsInConfig(const configuration::Recording& recording, const std::filesystem::path& home) {
        std::vector<std::filesystem::path> files;
        for (const auto& call : recording.calls) {
            if (call.function != "plugin.load" || call.arguments.empty())
                continue;
            if (const auto* path = call.arguments.front().string(); path != nullptr && !path->empty())
                if (auto file = expanded(*path, home); !contains(files, file))
                    files.push_back(std::move(file));
        }
        return files;
    }

    std::vector<std::filesystem::path> pluginsInConfig(std::string_view hyprlangConfig, const std::filesystem::path& home) {
        std::vector<std::filesystem::path> files;
        for (const auto line : hyprlangConfig | std::views::split('\n')) {
            std::string_view text{line.begin(), line.end()};
            text             = text.substr(0, text.find('#'));
            const auto equal = text.find('=');
            if (equal == std::string_view::npos || trimmed(text.substr(0, equal)) != "plugin")
                continue;
            if (const auto path = trimmed(text.substr(equal + 1)); !path.empty())
                if (auto file = expanded(path, home); !contains(files, file))
                    files.push_back(std::move(file));
        }
        return files;
    }

}
