// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/default-apps.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
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
                if (const auto piece = trim(std::string_view{part.begin(), part.end()}); !piece.empty())
                    parts.push_back(piece);
            return parts;
        }

        std::string fileText(const std::filesystem::path& file) {
            std::ifstream     stream{file, std::ios::binary};
            std::stringstream text;
            text << stream.rdbuf();
            return text.str();
        }

        std::filesystem::path folderOf(const foundation::Environment& environment, const char* variable, std::string_view fallback) {
            if (const auto set = foundation::variable(environment, variable); set && std::filesystem::path{*set}.is_absolute())
                return *set;
            return std::filesystem::path{foundation::variable(environment, "HOME").value_or("")} / fallback;
        }

        std::vector<std::filesystem::path> foldersOf(const foundation::Environment& environment, const char* variable, std::string_view fallback) {
            std::vector<std::filesystem::path> folders;
            for (const auto folder : split(foundation::variable(environment, variable).value_or(""), ':'))
                if (std::filesystem::path{folder}.is_absolute())
                    folders.emplace_back(folder);
            if (folders.empty())
                for (const auto folder : split(fallback, ':'))
                    folders.emplace_back(folder);
            return folders;
        }

        std::vector<std::string> desktopNames(const foundation::Environment& environment) {
            std::vector<std::string> names;
            for (const auto name : split(foundation::variable(environment, "XDG_CURRENT_DESKTOP").value_or(""), ':')) {
                std::string lower{name};
                std::ranges::transform(lower, lower.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
                names.push_back(std::move(lower));
            }
            return names;
        }

        // The value of `key` in `group`.
        std::optional<std::string_view> valueIn(std::string_view text, std::string_view group, std::string_view key) {
            bool inside = false;
            for (const auto line : split(text, '\n')) {
                if (line.starts_with('[')) {
                    inside = line == std::format("[{}]", group);
                    continue;
                }
                const auto equals = line.find('=');
                if (inside && equals != std::string_view::npos && trim(line.substr(0, equals)) == key)
                    return trim(line.substr(equals + 1));
            }
            return std::nullopt;
        }

        const std::array<DefaultKind, 8> kinds{{
            {"Web browser", "opens links and web pages", {"x-scheme-handler/https", "x-scheme-handler/http", "text/html", "application/xhtml+xml"}},
            {"Email", "opens email links", {"x-scheme-handler/mailto"}},
            {"File manager", "opens folders", {"inode/directory"}},
            {"Text editor", "opens text files", {"text/plain"}},
            {"Image viewer", "opens pictures", {"image/png", "image/jpeg", "image/gif", "image/webp", "image/bmp", "image/tiff", "image/svg+xml"}},
            {"PDF viewer", "opens PDF documents", {"application/pdf"}},
            {"Video player", "opens videos", {"video/mp4", "video/x-matroska", "video/webm", "video/quicktime", "video/x-msvideo"}},
            {"Music player", "opens songs", {"audio/mpeg", "audio/flac", "audio/ogg", "audio/x-wav", "audio/mp4"}},
        }};

    }

    std::span<const DefaultKind> defaultKinds() {
        return kinds;
    }

    std::vector<std::filesystem::path> applicationFolders(const foundation::Environment& environment) {
        std::vector<std::filesystem::path> folders{folderOf(environment, "XDG_DATA_HOME", ".local/share") / "applications"};
        for (const auto& folder : foldersOf(environment, "XDG_DATA_DIRS", "/usr/local/share:/usr/share"))
            folders.push_back(folder / "applications");
        return folders;
    }

    std::vector<Application> applications(std::span<const std::filesystem::path> folders) {
        std::vector<Application> found;
        std::vector<std::string> seen;
        for (const auto& folder : folders) {
            std::error_code                    error;
            std::vector<std::filesystem::path> files;
            for (const auto& item : std::filesystem::recursive_directory_iterator{folder, error})
                if (item.path().extension() == ".desktop" && item.is_regular_file(error))
                    files.push_back(item.path());
            std::ranges::sort(files);
            for (const auto& file : files) {
                // A file in a subfolder is named with a dash for each folder, as the
                // specification has it.
                auto id = std::filesystem::relative(file, folder).string();
                std::ranges::replace(id, '/', '-');
                if (std::ranges::contains(seen, id))
                    continue;
                seen.push_back(id);
                const auto text = fileText(file);
                if (valueIn(text, "Desktop Entry", "Hidden") == "true" || valueIn(text, "Desktop Entry", "Type").value_or("Application") != "Application")
                    continue;
                Application application{.id = id, .name = std::string{valueIn(text, "Desktop Entry", "Name").value_or(file.stem().string())}, .types = {}, .terminal = false};
                for (const auto type : split(valueIn(text, "Desktop Entry", "MimeType").value_or(""), ';'))
                    application.types.emplace_back(type);
                application.terminal = std::ranges::contains(split(valueIn(text, "Desktop Entry", "Categories").value_or(""), ';'), std::string_view{"TerminalEmulator"});
                found.push_back(std::move(application));
            }
        }
        std::ranges::stable_sort(found, {}, &Application::name);
        return found;
    }

    std::vector<std::filesystem::path> mimeappsFiles(const foundation::Environment& environment) {
        const auto                         names = desktopNames(environment);
        std::vector<std::filesystem::path> files;
        const auto                         addFrom = [&](const std::filesystem::path& folder) {
            for (const auto& name : names)
                files.push_back(folder / std::format("{}-mimeapps.list", name));
            files.push_back(folder / "mimeapps.list");
        };
        addFrom(folderOf(environment, "XDG_CONFIG_HOME", ".config"));
        for (const auto& folder : foldersOf(environment, "XDG_CONFIG_DIRS", "/etc/xdg"))
            addFrom(folder);
        addFrom(folderOf(environment, "XDG_DATA_HOME", ".local/share") / "applications");
        for (const auto& folder : foldersOf(environment, "XDG_DATA_DIRS", "/usr/local/share:/usr/share"))
            addFrom(folder / "applications");
        return files;
    }

    std::filesystem::path userMimeappsFile(const foundation::Environment& environment) {
        const auto folder = folderOf(environment, "XDG_CONFIG_HOME", ".config");
        for (const auto& name : desktopNames(environment))
            if (std::error_code error; std::filesystem::exists(folder / std::format("{}-mimeapps.list", name), error))
                return folder / std::format("{}-mimeapps.list", name);
        return folder / "mimeapps.list";
    }

    std::optional<std::string> defaultFor(std::span<const std::string> lists, std::string_view type, std::span<const Application> installed) {
        for (const auto& list : lists)
            if (const auto value = valueIn(list, "Default Applications", type))
                for (const auto id : split(*value, ';'))
                    if (std::ranges::contains(installed, id, &Application::id))
                        return std::string{id};
        return std::nullopt;
    }

    std::optional<std::string> fallbackFor(std::span<const std::string> lists, std::span<const std::string> caches, std::string_view type,
                                           std::span<const Application> installed) {
        const auto firstIn = [&](std::span<const std::string> texts, std::string_view group) -> std::optional<std::string> {
            for (const auto& text : texts)
                if (const auto value = valueIn(text, group, type))
                    for (const auto id : split(*value, ';'))
                        if (std::ranges::contains(installed, id, &Application::id))
                            return std::string{id};
            return std::nullopt;
        };
        if (auto added = firstIn(lists, "Added Associations"))
            return added;
        return firstIn(caches, "MIME Cache");
    }

    std::string withDefault(std::string_view list, std::span<const std::string> types, std::string_view id) {
        const auto lineFor = [id](std::string_view type, std::optional<std::string_view> before) {
            std::string value = std::format("{};", id);
            for (const auto other : split(before.value_or(""), ';'))
                if (other != id)
                    value += std::format("{};", other);
            return std::format("{}={}\n", type, value);
        };
        std::string              text;
        std::vector<std::string> done;
        bool                     inside = false;
        bool                     group  = false;
        // The new keys go at the end of the group, before the blank lines that end it.
        const auto finish = [&] {
            std::string added;
            for (const auto& type : types)
                if (!std::ranges::contains(done, type))
                    added += lineFor(type, std::nullopt);
            auto at = text.size();
            while (at >= 2 && text[at - 1] == '\n' && text[at - 2] == '\n')
                --at;
            text.insert(at, added);
        };
        std::size_t at = 0;
        while (at < list.size()) {
            const auto newline = list.find('\n', at);
            const auto end     = newline == std::string_view::npos ? list.size() : newline + 1;
            const auto line    = list.substr(at, end - at);
            const auto inner   = trim(line.substr(0, line.size() - (line.ends_with('\n') ? 1 : 0)));
            at                 = end;
            if (inner.starts_with('[')) {
                if (inside)
                    finish();
                inside = inner == "[Default Applications]";
                group  = group || inside;
            }
            const auto equals = inner.find('=');
            const auto key    = equals == std::string_view::npos ? std::string_view{} : trim(inner.substr(0, equals));
            if (inside && std::ranges::contains(types, key) && !std::ranges::contains(done, key)) {
                text += lineFor(key, trim(inner.substr(equals + 1)));
                done.emplace_back(key);
                continue;
            }
            text += line;
            if (!line.ends_with('\n'))
                text += '\n';
        }
        if (inside)
            finish();
        if (!group) {
            if (!text.empty())
                text += '\n';
            text += "[Default Applications]\n";
            finish();
        }
        return text;
    }

    std::filesystem::path terminalsFile(const foundation::Environment& environment) {
        return folderOf(environment, "XDG_CONFIG_HOME", ".config") / "xdg-terminals.list";
    }

    std::optional<std::string> preferredTerminal(std::string_view list) {
        for (const auto line : split(list, '\n'))
            if (!line.starts_with('#'))
                return std::string{line};
        return std::nullopt;
    }

    std::string withTerminal(std::string_view list, std::string_view id) {
        std::string text = std::format("{}\n", id);
        for (const auto line : split(list, '\n'))
            if (line != id)
                text += std::format("{}\n", line);
        return text;
    }

}
