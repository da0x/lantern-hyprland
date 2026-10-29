// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/cursor-theme.hpp"

#include "configuration/lua-edit.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace desktop {

    namespace {

        struct Recommended {
            std::string_view name;
            std::string_view package;
        };

        // Cursor themes in Arch Linux's own repositories, by the folder each installs.
        constexpr std::array recommended{
            Recommended{"breeze_cursors", "breeze-cursors"},       Recommended{"Breeze_Light", "breeze-cursors"},
            Recommended{"capitaine-cursors", "capitaine-cursors"}, Recommended{"capitaine-cursors-light", "capitaine-cursors"},
            Recommended{"Vimix-cursors", "vimix-cursors"},         Recommended{"Vimix-white-cursors", "vimix-cursors"},
            Recommended{"Vanilla-DMZ", "xcursor-vanilla-dmz"},     Recommended{"Vanilla-DMZ-AA", "xcursor-vanilla-dmz-aa"},
            Recommended{"ComixCursors-White", "xcursor-comix"},    Recommended{"ComixCursors-Black", "xcursor-comix"},
            Recommended{"Oxygen_White", "oxygen-cursors"},         Recommended{"Oxygen_Blue", "oxygen-cursors"},
            Recommended{"whiteglass", "xcursor-themes"},           Recommended{"redglass", "xcursor-themes"},
        };

        std::string readAll(const std::filesystem::path& file) {
            std::ifstream     stream{file, std::ios::binary};
            std::stringstream text;
            text << stream.rdbuf();
            return text.str();
        }

        // Where themes are installed, the user's first, as XCursor looks for them.
        std::vector<std::filesystem::path> iconFolders(const foundation::Environment& environment) {
            const std::filesystem::path home = foundation::variable(environment, "HOME").value_or("");
            std::vector<std::filesystem::path> folders{home / ".local/share/icons", home / ".icons"};
            std::string data = foundation::variable(environment, "XDG_DATA_DIRS").value_or("/usr/local/share:/usr/share");
            for (std::size_t at = 0; at <= data.size();) {
                const auto end = std::min(data.find(':', at), data.size());
                if (end > at)
                    folders.push_back(std::filesystem::path{data.substr(at, end - at)} / "icons");
                at = end + 1;
            }
            return folders;
        }

        // A line of an index.theme's [Icon Theme]: Name or Inherits.
        std::optional<std::string> indexValue(const std::filesystem::path& folder, std::string_view key) {
            std::istringstream lines{readAll(folder / "index.theme")};
            for (std::string line; std::getline(lines, line);) {
                if (!line.starts_with(key))
                    continue;
                const auto equals = line.find('=');
                if (equals == std::string::npos || line.find_first_not_of(' ', key.size()) != equals)
                    continue;
                auto value = line.substr(equals + 1);
                value.erase(0, value.find_first_not_of(' '));
                while (!value.empty() && (value.back() == ' ' || value.back() == '\r'))
                    value.pop_back();
                return value;
            }
            return std::nullopt;
        }

        std::optional<std::filesystem::path> installedFolder(std::string_view name, const foundation::Environment& environment) {
            std::error_code error;
            for (const auto& folder : iconFolders(environment))
                if (std::filesystem::is_directory(folder / name / "cursors", error))
                    return folder / name;
            return std::nullopt;
        }

        std::uint32_t little(const std::string& bytes, std::size_t at) {
            if (at + 4 > bytes.size())
                return 0;
            std::uint32_t value = 0;
            for (int index = 3; index >= 0; --index)
                value = (value << 8U) | static_cast<unsigned char>(bytes[at + static_cast<std::size_t>(index)]);
            return value;
        }

        void big(std::vector<std::uint8_t>& out, std::uint32_t value) {
            for (int shift = 24; shift >= 0; shift -= 8)
                out.push_back(static_cast<std::uint8_t>(value >> static_cast<unsigned>(shift)));
        }

        std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
            std::uint32_t crc = 0xFFFFFFFFU;
            for (std::size_t index = 0; index < size; ++index) {
                crc ^= data[index];
                for (int bit = 0; bit < 8; ++bit)
                    crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
            }
            return ~crc;
        }

        // An RGBA image as a PNG, its pixels stored without compression: the images are a
        // few kilobytes and only drawn, never kept.
        std::vector<std::uint8_t> png(std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>& rgba) {
            std::vector<std::uint8_t> raw;
            for (std::uint32_t row = 0; row < height; ++row) {
                raw.push_back(0);
                raw.insert(raw.end(), rgba.begin() + row * width * 4, rgba.begin() + (row + 1) * width * 4);
            }
            std::vector<std::uint8_t> zlib{0x78, 0x01};
            std::uint32_t a = 1, b = 0;
            for (const auto byte : raw) {
                a = (a + byte) % 65521;
                b = (b + a) % 65521;
            }
            for (std::size_t at = 0; at < raw.size() || at == 0; at += 65535) {
                const auto length = static_cast<std::uint16_t>(std::min<std::size_t>(65535, raw.size() - at));
                zlib.push_back(at + length >= raw.size() ? 1 : 0);
                zlib.push_back(static_cast<std::uint8_t>(length & 0xFFU));
                zlib.push_back(static_cast<std::uint8_t>(length >> 8U));
                zlib.push_back(static_cast<std::uint8_t>(~length & 0xFFU));
                zlib.push_back(static_cast<std::uint8_t>((~length >> 8U) & 0xFFU));
                zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + length));
                if (raw.empty())
                    break;
            }
            big(zlib, (b << 16U) | a);

            std::vector<std::uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
            const auto chunk = [&out](const char* type, const std::vector<std::uint8_t>& data) {
                big(out, static_cast<std::uint32_t>(data.size()));
                const auto start = out.size();
                out.insert(out.end(), type, type + 4);
                out.insert(out.end(), data.begin(), data.end());
                big(out, crc32(out.data() + start, out.size() - start));
            };
            std::vector<std::uint8_t> header;
            big(header, width);
            big(header, height);
            header.insert(header.end(), {8, 6, 0, 0, 0});
            chunk("IHDR", header);
            chunk("IDAT", zlib);
            chunk("IEND", {});
            return out;
        }

    }

    std::vector<CursorTheme> cursorThemes(const foundation::Environment& environment) {
        std::map<std::string, CursorTheme> installed;
        std::error_code                    error;
        for (const auto& folder : iconFolders(environment)) {
            for (const auto& entry : std::filesystem::directory_iterator{folder, error}) {
                const auto name = entry.path().filename().string();
                // The first found wins, as for XCursor; "default" only points at another.
                if (name == "default" || installed.contains(name) || !std::filesystem::is_directory(entry.path() / "cursors", error))
                    continue;
                installed[name] = CursorTheme{.name = name, .title = indexValue(entry.path(), "Name").value_or(name), .folder = entry.path(), .package = {}};
            }
        }
        std::vector<CursorTheme> themes;
        for (auto& [name, theme] : installed)
            themes.push_back(std::move(theme));
        std::ranges::sort(themes, [](const auto& left, const auto& right) { return left.title < right.title; });
        for (const auto& [name, package] : recommended) {
            const auto found = std::ranges::find(themes, name, &CursorTheme::name);
            if (found != themes.end())
                found->package = package;
            else
                themes.push_back(CursorTheme{.name = std::string{name}, .title = std::string{name}, .folder = {}, .package = std::string{package}});
        }
        return themes;
    }

    std::string currentCursorTheme(const configuration::MainConfig& config, const foundation::Environment& environment) {
        std::string name;
        if (config.lua())
            name = configuration::luaEnv(readAll(config.file), "XCURSOR_THEME").value_or("");
        if (name.empty())
            if (const auto set = gsetting("org.gnome.desktop.interface", "cursor-theme", environment); set && set->size() > 2)
                name = set->substr(1, set->size() - 2);
        if (name.empty())
            name = "default";
        // "default" names no theme of its own: it inherits one.
        for (int step = 0; step < 4 && !installedFolder(name, environment); ++step) {
            std::optional<std::string> inherits;
            std::error_code            error;
            for (const auto& folder : iconFolders(environment))
                if (!inherits && std::filesystem::exists(folder / name / "index.theme", error))
                    inherits = indexValue(folder / name, "Inherits");
            if (!inherits)
                break;
            name = inherits->substr(0, inherits->find(','));
        }
        return name;
    }

    std::optional<std::vector<std::uint8_t>> cursorImage(const std::filesystem::path& folder, const std::vector<std::string_view>& names, int size) {
        std::string bytes;
        for (const auto name : names)
            if (bytes.empty())
                bytes = readAll(folder / "cursors" / name);
        // An XCursor file: "Xcur", the header's size, a version, and a table of its images.
        if (bytes.size() < 16 || bytes.compare(0, 4, "Xcur") != 0)
            return std::nullopt;
        constexpr std::uint32_t imageType = 0xFFFD0002U;
        const auto              count     = little(bytes, 12);
        std::optional<std::uint32_t> best;
        std::uint32_t                bestSize = 0;
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto entry = 16 + static_cast<std::size_t>(index) * 12;
            if (little(bytes, entry) != imageType)
                continue;
            const auto nominal = little(bytes, entry + 4);
            const auto distance = [size](std::uint32_t candidate) { return std::abs(static_cast<int>(candidate) - size); };
            if (!best || distance(nominal) < distance(bestSize)) {
                best     = little(bytes, entry + 8);
                bestSize = nominal;
            }
        }
        if (!best)
            return std::nullopt;
        const auto width  = little(bytes, *best + 16);
        const auto height = little(bytes, *best + 20);
        const auto pixels = static_cast<std::size_t>(*best) + 36;
        if (width == 0 || height == 0 || width > 512 || height > 512 || pixels + static_cast<std::size_t>(width) * height * 4 > bytes.size())
            return std::nullopt;
        // Premultiplied ARGB, little-endian, to plain RGBA.
        std::vector<std::uint8_t> rgba;
        rgba.reserve(static_cast<std::size_t>(width) * height * 4);
        for (std::size_t at = pixels; at < pixels + static_cast<std::size_t>(width) * height * 4; at += 4) {
            const auto argb  = little(bytes, at);
            const auto alpha = (argb >> 24U) & 0xFFU;
            const auto plain = [alpha](std::uint32_t channel) { return static_cast<std::uint8_t>(alpha == 0 ? 0 : std::min<std::uint32_t>(255, channel * 255 / alpha)); };
            rgba.insert(rgba.end(), {plain((argb >> 16U) & 0xFFU), plain((argb >> 8U) & 0xFFU), plain(argb & 0xFFU), static_cast<std::uint8_t>(alpha)});
        }
        return png(width, height, rgba);
    }

    std::expected<std::vector<configuration::FileChange>, std::string> planCursorTheme(const CursorTheme& theme, const configuration::MainConfig& config,
                                                                                      const foundation::Environment& environment) {
        if (!config.lua())
            return std::unexpected{"the cursor is set only in a Lua config (hyprland.lua) so far"};
        std::vector<configuration::FileChange> changes;
        std::error_code                        error;
        const auto                             file = std::filesystem::canonical(config.file, error);
        if (error)
            return std::unexpected{std::format("cannot read {}: {}", config.file.string(), error.message())};
        const std::filesystem::path home = foundation::variable(environment, "HOME").value_or("");
        for (auto planned : {configuration::planEdit(file, [&theme](std::string_view text) { return configuration::withLuaEnv(text, "XCURSOR_THEME", theme.name); }),
                             // X11 apps take the cursor of the "default" theme, which inherits the one chosen.
                             configuration::planEdit(home / ".icons/default/index.theme", [&theme](std::string_view text) {
                                 std::string result;
                                 bool        set = false;
                                 std::istringstream lines{std::string{text}};
                                 for (std::string line; std::getline(lines, line);) {
                                     if (line.starts_with("Inherits")) {
                                         line = "Inherits=" + theme.name;
                                         set  = true;
                                     }
                                     result += line + '\n';
                                 }
                                 if (!set)
                                     result += (result.empty() ? "[Icon Theme]\n" : "") + std::string{"Inherits="} + theme.name + '\n';
                                 return result;
                             })}) {
            if (!planned)
                return std::unexpected{planned.error()};
            if (*planned)
                changes.push_back(std::move(**planned));
        }
        return changes;
    }

    std::vector<GSetting> cursorGsettings(const CursorTheme& theme, const foundation::Environment& environment) {
        return gsettingChanges({{"org.gnome.desktop.interface", "cursor-theme", "", std::format("'{}'", theme.name)}}, environment);
    }

}
