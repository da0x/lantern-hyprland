// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/autostart-entries.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <ranges>
#include <sstream>

namespace desktop {

    namespace {

        std::string_view trim(std::string_view text) {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
                text.remove_suffix(1);
            return text;
        }

        std::vector<std::string_view> split(std::string_view text, char separator) {
            std::vector<std::string_view> parts;
            for (const auto part : text | std::views::split(separator))
                if (const auto piece = std::string_view{part.begin(), part.end()}; !piece.empty())
                    parts.push_back(piece);
            return parts;
        }

        // The keys of the [Desktop Entry] group.
        std::map<std::string, std::string, std::less<>> entryKeys(std::string_view text) {
            std::map<std::string, std::string, std::less<>> keys;
            bool                                            inside = false;
            for (const auto line : split(text, '\n')) {
                const auto inner = trim(line);
                if (inner.starts_with('[')) {
                    inside = inner == "[Desktop Entry]";
                    continue;
                }
                const auto equals = inner.find('=');
                if (inside && equals != std::string_view::npos && !inner.starts_with('#'))
                    keys.try_emplace(std::string{trim(inner.substr(0, equals))}, trim(inner.substr(equals + 1)));
            }
            return keys;
        }

        std::optional<std::string> value(const std::map<std::string, std::string, std::less<>>& keys, std::string_view key) {
            const auto found = keys.find(key);
            return found == keys.end() ? std::nullopt : std::optional{found->second};
        }

        bool mentions(std::string_view list, std::span<const std::string_view> desktops) {
            return std::ranges::any_of(split(list, ';'), [&](std::string_view one) { return std::ranges::find(desktops, one) != desktops.end(); });
        }

        std::string fileText(const std::filesystem::path& file) {
            std::ifstream     stream{file, std::ios::binary};
            std::stringstream text;
            text << stream.rdbuf();
            return text.str();
        }

        std::filesystem::path configHome(const foundation::Environment& environment) {
            if (const auto home = foundation::variable(environment, "XDG_CONFIG_HOME"); home && std::filesystem::path{*home}.is_absolute())
                return *home;
            return std::filesystem::path{foundation::variable(environment, "HOME").value_or("")} / ".config";
        }

    }

    std::filesystem::path userAutostartFolder(const foundation::Environment& environment) {
        return configHome(environment) / "autostart";
    }

    std::vector<std::filesystem::path> systemAutostartFolders(const foundation::Environment& environment) {
        std::vector<std::filesystem::path> folders;
        const auto                         dirs = foundation::variable(environment, "XDG_CONFIG_DIRS").value_or("");
        for (const auto dir : split(dirs, ':'))
            if (std::filesystem::path{dir}.is_absolute())
                folders.push_back(std::filesystem::path{dir} / "autostart");
        if (folders.empty())
            folders.emplace_back("/etc/xdg/autostart");
        return folders;
    }

    std::vector<AutostartEntry> autostartEntries(const std::filesystem::path& userFolder, std::span<const std::filesystem::path> systemFolders, std::string_view desktops) {
        const auto                    current = split(desktops, ':');
        std::vector<AutostartEntry>   entries;
        std::vector<std::filesystem::path> folders{userFolder};
        folders.insert(folders.end(), systemFolders.begin(), systemFolders.end());
        for (std::size_t index = 0; index < folders.size(); ++index) {
            std::error_code                    error;
            std::vector<std::filesystem::path> files;
            for (const auto& item : std::filesystem::directory_iterator{folders[index], error})
                if (item.path().extension() == ".desktop" && item.is_regular_file(error))
                    files.push_back(item.path());
            std::ranges::sort(files);
            for (const auto& file : files) {
                const auto id = file.filename().string();
                if (std::ranges::any_of(entries, [&](const AutostartEntry& entry) { return entry.id == id; }))
                    continue;
                const auto keys = entryKeys(fileText(file));
                AutostartEntry entry;
                entry.id      = id;
                entry.name    = value(keys, "Name").value_or(file.stem().string());
                entry.command = value(keys, "Exec").value_or("");
                entry.file    = file;
                entry.users   = index == 0;
                entry.on      = value(keys, "Hidden") != "true" && value(keys, "X-GNOME-Autostart-enabled") != "false";
                if (const auto only = value(keys, "OnlyShowIn"))
                    entry.forDesktop = mentions(*only, current);
                if (const auto notShown = value(keys, "NotShowIn"); notShown && mentions(*notShown, current))
                    entry.forDesktop = false;
                entries.push_back(std::move(entry));
            }
        }
        return entries;
    }

    std::string withEntryOn(std::string_view entry, bool on) {
        std::string text;
        bool        inside = false;
        bool        hidden = false;
        std::size_t header = std::string::npos;
        std::size_t at     = 0;
        while (at < entry.size()) {
            const auto newline = entry.find('\n', at);
            const auto end     = newline == std::string_view::npos ? entry.size() : newline + 1;
            const auto line    = entry.substr(at, end - at);
            const auto inner   = trim(line.substr(0, line.size() - (line.ends_with('\n') ? 1 : 0)));
            at                 = end;
            if (inner.starts_with('[')) {
                inside = inner == "[Desktop Entry]";
                text += line;
                if (inside && header == std::string::npos)
                    header = text.size();
                continue;
            }
            const auto equals = inner.find('=');
            const auto key    = equals == std::string_view::npos ? std::string_view{} : trim(inner.substr(0, equals));
            if (inside && key == "Hidden") {
                hidden = true;
                text += std::format("Hidden={}\n", on ? "false" : "true");
            } else if (inside && key == "X-GNOME-Autostart-enabled")
                text += std::format("X-GNOME-Autostart-enabled={}\n", on ? "true" : "false");
            else
                text += line;
        }
        if (!hidden && !on) {
            if (header == std::string::npos)
                return std::string{"[Desktop Entry]\nHidden=true\n"} + text;
            if (!text.substr(0, header).ends_with('\n')) {
                text.insert(header, "\n");
                ++header;
            }
            text.insert(header, "Hidden=true\n");
        }
        return text;
    }

}
