// SPDX-License-Identifier: GPL-3.0-or-later
#include "ipc/plugins.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace ipc {

    std::expected<std::vector<LoadedPlugin>, std::string> parseLoadedPlugins(std::string_view reply) {
        // With nothing loaded, Hyprland answers in words even when asked for JSON.
        if (reply.starts_with("no plugins loaded"))
            return std::vector<LoadedPlugin>{};
        const auto document = nlohmann::json::parse(reply, nullptr, false);
        if (document.is_discarded() || !document.is_array())
            return std::unexpected{"the plugin list is not a JSON array"};

        std::vector<LoadedPlugin> plugins;
        for (const auto& entry : document) {
            if (!entry.is_object())
                continue;
            const auto text = [&entry](const char* key) {
                const auto found = entry.find(key);
                return found != entry.end() && found->is_string() ? found->get<std::string>() : std::string{};
            };
            plugins.push_back(LoadedPlugin{.name = text("name"), .author = text("author"), .version = text("version"), .description = text("description")});
        }
        return plugins;
    }

    std::vector<std::filesystem::path> mappedLibraries(const Instance& instance, const std::filesystem::path& processRoot) {
        std::ifstream lock{instance.folder / "hyprland.lock"};
        std::string   process;
        if (!std::getline(lock, process) || process.empty() || !std::ranges::all_of(process, [](char c) { return c >= '0' && c <= '9'; }))
            return {};

        // Each line ends in the mapped file's path, after five fields; anonymous mappings
        // have none, and the kernel's own are in brackets.
        std::ifstream                      maps{processRoot / process / "maps"};
        std::vector<std::filesystem::path> libraries;
        std::string                        line;
        while (std::getline(maps, line)) {
            const auto slash = line.find('/');
            if (slash == std::string::npos)
                continue;
            std::filesystem::path file{line.substr(slash)};
            const auto            name = file.filename().string();
            if (!name.ends_with(".so") && !name.contains(".so."))
                continue;
            if (std::ranges::find(libraries, file) == libraries.end())
                libraries.push_back(std::move(file));
        }
        return libraries;
    }

}
