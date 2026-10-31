// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/wallpaper.hpp"

#include "configuration/autostart.hpp"
#include "configuration/lua-edit.hpp"
#include "desktop/process.hpp"
#include "desktop/system.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <cstdint>
#include <fstream>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <system_error>

namespace desktop {

    namespace {

        constexpr std::array fits{
            FitInfo{FitMode::cover, "cover", "Cover"},
            FitInfo{FitMode::contain, "contain", "Contain"},
            FitInfo{FitMode::fill, "fill", "Fill"},
            FitInfo{FitMode::tile, "tile", "Tile"},
        };

        std::string_view trim(std::string_view text) {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
                text.remove_suffix(1);
            return text;
        }

        // Where a value sits in the text, so it can be replaced without touching the rest.
        struct Value {
            std::size_t      begin = 0;
            std::size_t      end   = 0;
            std::string_view text;
        };

        struct Block {
            std::optional<Value> monitor;
            std::optional<Value> path;
            std::optional<Value> fit;
            // The start of the line with the closing brace, and the indentation of the keys.
            std::size_t          close  = 0;
            std::string          indent = "    ";
        };

        struct Parsed {
            std::vector<Block> blocks;
            bool               oldFormat = false;
        };

        // hyprpaper.conf is hyprlang: `key = value` lines, `#` comments, and named blocks.
        // Only what Lantern needs is read: the wallpaper blocks' keys.
        Parsed parse(std::string_view config) {
            Parsed                parsed;
            std::optional<Block>  open;
            std::size_t           at = 0;
            while (at < config.size()) {
                const auto end   = std::min(config.find('\n', at), config.size());
                const auto line  = config.substr(at, end - at);
                auto       code  = line.substr(0, line.find('#'));
                const auto inner = trim(code);
                if (!open) {
                    if (inner == "wallpaper {" || inner == "wallpaper{")
                        open = Block{};
                    else if (const auto equals = inner.find('='); equals != std::string_view::npos) {
                        const auto key = trim(inner.substr(0, equals));
                        if (key == "wallpaper" || key == "preload")
                            parsed.oldFormat = true;
                    }
                } else if (inner == "}") {
                    open->close = at;
                    parsed.blocks.push_back(*open);
                    open.reset();
                } else if (const auto equals = inner.find('='); equals != std::string_view::npos) {
                    const auto key        = trim(inner.substr(0, equals));
                    const auto keyAt      = line.find(key);
                    const auto valueStart = line.find('=', keyAt) + 1;
                    auto       value      = trim(code.substr(valueStart));
                    // The value's own place in the line, after any spaces.
                    const auto valueAt    = value.empty() ? valueStart : code.find(value, valueStart);
                    const Value found{at + valueAt, at + valueAt + value.size(), value};
                    open->indent = std::string{line.substr(0, keyAt)};
                    if (key == "monitor")
                        open->monitor = found;
                    else if (key == "path")
                        open->path = found;
                    else if (key == "fit_mode")
                        open->fit = found;
                }
                at = end + 1;
            }
            return parsed;
        }

        bool isFallback(const Block& block) {
            return !block.monitor || block.monitor->text.empty();
        }

        std::filesystem::path homeOf(const foundation::Environment& environment) {
            return foundation::variable(environment, "HOME").value_or("");
        }

        std::optional<std::string> readFile(const std::filesystem::path& file) {
            std::ifstream stream{file, std::ios::binary};
            if (!stream)
                return std::nullopt;
            std::stringstream text;
            text << stream.rdbuf();
            return text.str();
        }

        // A version 4 UUID, as 8-4-4-4-12 lowercase hexadecimal digits.
        std::string randomUuid() {
            std::random_device        source;
            std::array<std::uint8_t, 16> bytes{};
            for (auto& byte : bytes)
                byte = static_cast<std::uint8_t>(source());
            bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0FU) | 0x40U);
            bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3FU) | 0x80U);
            std::string text;
            for (std::size_t index = 0; index < bytes.size(); ++index) {
                if (index == 4 || index == 6 || index == 8 || index == 10)
                    text += '-';
                text += std::format("{:02x}", bytes[index]);
            }
            return text;
        }

        bool isImage(const std::filesystem::path& file) {
            auto extension = file.extension().string();
            std::ranges::transform(extension, extension.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            constexpr std::array images{".png", ".jpg", ".jpeg", ".webp", ".jxl"};
            return std::ranges::contains(images, std::string_view{extension});
        }

    }

    std::span<const FitInfo> fitModes() {
        return fits;
    }

    Requirement wallpaperRequirement(WallpaperProgram program, const foundation::Environment& environment) {
        Requirement requirement;
        std::string_view binary;
        if (program == WallpaperProgram::hyprpaper) {
            requirement = Requirement{
                .name     = "hyprpaper",
                .purpose  = "Hyprland's own wallpaper program. Without a wallpaper program, Hyprland shows its default wallpaper.",
                .upstream = "https://github.com/hyprwm/hyprpaper",
                .package  = "hyprpaper",
                .found    = std::nullopt,
            };
            binary = "hyprpaper";
        } else {
            requirement = Requirement{
                .name     = "awww",
                .purpose  = "A wallpaper program that fades from one image to the next, and shows the last one again when it starts.",
                .upstream = "https://codeberg.org/LGFae/awww",
                .package  = "awww",
                .found    = std::nullopt,
            };
            binary = "awww-daemon";
        }
        if (const auto found = findProgram(binary, foundation::variable(environment, "PATH").value_or("")))
            requirement.found = found->string();
        return requirement;
    }

    std::expected<std::vector<std::string>, std::string> awwwArguments(const Wallpaper& wallpaper) {
        std::string_view resize;
        switch (wallpaper.fit) {
            case FitMode::cover: resize = "crop"; break;
            case FitMode::contain: resize = "fit"; break;
            case FitMode::fill: resize = "stretch"; break;
            case FitMode::tile: return std::unexpected{"awww cannot tile an image"};
        }
        return std::vector<std::string>{"img", wallpaper.image.string(), "--resize", std::string{resize}, "--transition-type", "fade"};
    }

    std::optional<std::filesystem::path> awwwImage(std::string_view queryJson) {
        const auto json = nlohmann::json::parse(queryJson, nullptr, false);
        if (!json.is_object())
            return std::nullopt;
        for (const auto& [name, outputs] : json.items()) {
            if (!outputs.is_array())
                continue;
            for (const auto& output : outputs) {
                const auto displaying = output.find("displaying");
                if (displaying == output.end() || !displaying->is_object())
                    continue;
                if (const auto image = displaying->find("image"); image != displaying->end() && image->is_string())
                    return std::filesystem::path{image->get<std::string>()};
            }
        }
        return std::nullopt;
    }

    std::filesystem::path hyprpaperConfigFile(const foundation::Environment& environment) {
        if (const auto home = foundation::variable(environment, "XDG_CONFIG_HOME"); home && std::filesystem::path{*home}.is_absolute())
            return std::filesystem::path{*home} / "hypr" / "hyprpaper.conf";
        return homeOf(environment) / ".config" / "hypr" / "hyprpaper.conf";
    }

    std::optional<Wallpaper> fallbackWallpaper(std::string_view config, const std::filesystem::path& home) {
        for (const auto& block : parse(config).blocks) {
            if (!isFallback(block) || !block.path)
                continue;
            std::filesystem::path image{std::string{block.path->text}};
            if (block.path->text.starts_with("~/"))
                image = home / std::string{block.path->text.substr(2)};
            Wallpaper wallpaper{image, FitMode::cover};
            if (block.fit)
                for (const auto& info : fits)
                    if (info.word == block.fit->text)
                        wallpaper.fit = info.mode;
            return wallpaper;
        }
        return std::nullopt;
    }

    std::vector<std::string> monitorsWithOwnWallpaper(std::string_view config) {
        std::vector<std::string> monitors;
        for (const auto& block : parse(config).blocks)
            if (!isFallback(block))
                monitors.emplace_back(block.monitor->text);
        return monitors;
    }

    std::expected<std::string, std::string> withFallbackWallpaper(std::string_view config, const Wallpaper& wallpaper) {
        const auto parsed = parse(config);
        if (parsed.oldFormat)
            return std::unexpected{"hyprpaper.conf is written in the format from before hyprpaper 0.8 (wallpaper = monitor,path), which Lantern does not change"};

        std::string_view fitWord = "cover";
        for (const auto& info : fits)
            if (info.mode == wallpaper.fit)
                fitWord = info.word;

        const auto found = std::ranges::find_if(parsed.blocks, isFallback);
        if (found == parsed.blocks.end()) {
            std::string text{config};
            if (!text.empty() && !text.ends_with('\n'))
                text += '\n';
            if (!text.empty())
                text += '\n';
            text += std::format("wallpaper {{\n    monitor =\n    path = {}\n    fit_mode = {}\n}}\n", wallpaper.image.string(), fitWord);
            return text;
        }

        // Replaced from the end of the text backwards, so earlier offsets stay right.
        struct Edit {
            std::size_t begin;
            std::size_t end;
            std::string text;
        };
        std::vector<Edit> edits;
        const auto&       block = *found;
        if (block.path)
            edits.push_back({block.path->begin, block.path->end, wallpaper.image.string()});
        if (block.fit)
            edits.push_back({block.fit->begin, block.fit->end, std::string{fitWord}});
        std::string added;
        if (!block.path)
            added += std::format("{}path = {}\n", block.indent, wallpaper.image.string());
        if (!block.fit)
            added += std::format("{}fit_mode = {}\n", block.indent, fitWord);
        if (!added.empty())
            edits.push_back({block.close, block.close, added});
        std::ranges::sort(edits, std::ranges::greater{}, &Edit::begin);

        std::string text{config};
        for (const auto& edit : edits)
            text.replace(edit.begin, edit.end - edit.begin, edit.text);
        return text;
    }

    std::expected<configuration::FileChange, std::string> planWallpaper(const Wallpaper& wallpaper, const foundation::Environment& environment) {
        const auto file    = hyprpaperConfigFile(environment);
        const auto current = configuration::planFile(file, "");
        if (!current)
            return std::unexpected{current.error()};
        auto text = withFallbackWallpaper(current->current.value_or(""), wallpaper);
        if (!text)
            return std::unexpected{text.error()};
        return configuration::FileChange{file, current->current, std::move(*text)};
    }

    std::expected<std::vector<std::string>, std::string> showWallpaperArguments(const Wallpaper& wallpaper) {
        const auto path = wallpaper.image.string();
        if (path.contains(','))
            return std::unexpected{std::format("{} has a comma in it, which hyprctl cannot pass on", path)};
        // hyprctl's words for the fit, which differ from the config's for fill.
        std::string_view fit = "cover";
        switch (wallpaper.fit) {
            case FitMode::cover: fit = "cover"; break;
            case FitMode::contain: fit = "contain"; break;
            case FitMode::fill: fit = "stretch"; break;
            case FitMode::tile: fit = "tile"; break;
        }
        return std::vector<std::string>{"hyprpaper", "wallpaper", std::format(", {}, {}", path, fit)};
    }

    namespace {

        constexpr auto workspaceBlock = "a wallpaper for each workspace";

        // The fit in the program's own words, as the commands take it.
        std::string_view commandFit(WallpaperProgram program, FitMode fit) {
            switch (fit) {
                case FitMode::cover: return program == WallpaperProgram::awww ? "crop" : "cover";
                case FitMode::contain: return program == WallpaperProgram::awww ? "fit" : "contain";
                case FitMode::fill: return "stretch";
                case FitMode::tile: return "tile";
            }
            return "cover";
        }

        std::optional<FitMode> fitFromCommand(std::string_view word) {
            if (word == "crop" || word == "cover")
                return FitMode::cover;
            if (word == "fit" || word == "contain")
                return FitMode::contain;
            if (word == "stretch")
                return FitMode::fill;
            if (word == "tile")
                return FitMode::tile;
            return std::nullopt;
        }

        std::expected<std::string, std::string> luaEntry(WallpaperProgram program, const Wallpaper& wallpaper) {
            const auto path = wallpaper.image.string();
            if (path.find_first_of("'\"\\\n") != std::string::npos)
                return std::unexpected{std::format("{} has a quote or a backslash in it, which the command cannot pass on", path)};
            if (program == WallpaperProgram::hyprpaper && path.contains(','))
                return std::unexpected{std::format("{} has a comma in it, which hyprctl cannot pass on", path)};
            if (program == WallpaperProgram::awww && wallpaper.fit == FitMode::tile)
                return std::unexpected{"awww cannot tile an image"};
            return std::format("{{ \"{}\", \"{}\" }}", path, commandFit(program, wallpaper.fit));
        }

    }

    namespace {

        // Each `{ "path", "fit" }` entry of the block, by what comes before it on its line:
        // a workspace's number, or the word rest.
        std::vector<std::pair<std::string, Wallpaper>> blockEntries(std::string_view config) {
            std::vector<std::pair<std::string, Wallpaper>> entries;
            const auto                                     block = configuration::luaBlock(config, workspaceBlock);
            if (!block)
                return entries;
            static const std::regex entry{R"re(^\s*(?:\[(\d+)\] =|local (rest) =) \{ "([^"]*)", "(\w+)" \},?\s*$)re"};
            std::istringstream      lines{std::string{*block}};
            for (std::string line; std::getline(lines, line);) {
                std::smatch match;
                if (!std::regex_match(line, match, entry))
                    continue;
                if (const auto fit = fitFromCommand(match[4].str()))
                    entries.emplace_back(match[1].matched ? match[1].str() : match[2].str(), Wallpaper{match[3].str(), *fit});
            }
            return entries;
        }

    }

    WorkspaceWallpapers workspaceWallpapersIn(std::string_view config) {
        WorkspaceWallpapers workspaces;
        for (const auto& [key, wallpaper] : blockEntries(config))
            if (key != "rest")
                workspaces[std::stoi(key)] = wallpaper;
        return workspaces;
    }

    std::optional<Wallpaper> restWallpaperIn(std::string_view config) {
        for (const auto& [key, wallpaper] : blockEntries(config))
            if (key == "rest")
                return wallpaper;
        return std::nullopt;
    }

    std::expected<std::string, std::string> workspaceWallpaperBlock(WallpaperProgram program, const Wallpaper& rest, const WorkspaceWallpapers& workspaces) {
        std::string text = "do\n    local wallpapers = {\n";
        for (const auto& [number, wallpaper] : workspaces) {
            const auto entry = luaEntry(program, wallpaper);
            if (!entry)
                return std::unexpected{entry.error()};
            text += std::format("        [{}] = {},\n", number, *entry);
        }
        const auto other = luaEntry(program, rest);
        if (!other)
            return std::unexpected{other.error()};
        text += std::format("    }}\n    local rest = {}\n", *other);
        text += "    hl.on(\"workspace.active\", function(workspace)\n"
                "        if not workspace.monitor then\n"
                "            return\n"
                "        end\n"
                "        local wallpaper = workspace.id and wallpapers[workspace.id] or rest\n";
        if (program == WallpaperProgram::awww)
            text += "        hl.exec_cmd(\"awww img -o \" .. workspace.monitor.name .. \" --resize \" .. wallpaper[2] .. \" --transition-type fade '\" .. wallpaper[1] .. \"'\")\n";
        else
            text += "        hl.exec_cmd(\"hyprctl hyprpaper wallpaper '\" .. workspace.monitor.name .. \", \" .. wallpaper[1] .. \", \" .. wallpaper[2] .. \"'\")\n";
        text += "    end)\nend\n";
        return text;
    }

    std::string_view loginCommand(WallpaperProgram program) {
        return program == WallpaperProgram::awww ? "awww-daemon" : "hyprpaper";
    }

    std::expected<std::vector<configuration::FileChange>, std::string> planWallpaperSetup(WallpaperProgram program, const Wallpaper& wallpaper,
                                                                                         const WorkspaceWallpapers& workspaces, const configuration::MainConfig& config, const foundation::Environment& environment) {
        std::vector<configuration::FileChange> changes;
        if (program == WallpaperProgram::hyprpaper) {
            auto change = planWallpaper(wallpaper, environment);
            if (!change)
                return std::unexpected{change.error()};
            if (change->changes())
                changes.push_back(std::move(*change));
        }

        std::error_code error;
        const auto      file = std::filesystem::canonical(config.file, error);
        if (error)
            return std::unexpected{std::format("cannot read {}: {}", config.file.string(), error.message())};
        auto current = configuration::planFile(file, "");
        if (!current)
            return std::unexpected{current.error()};
        const auto other = loginCommand(program == WallpaperProgram::awww ? WallpaperProgram::hyprpaper : WallpaperProgram::awww);
        const std::array<std::string_view, 1> instead{other};
        current->proposed = configuration::withAutostart(current->current.value_or(""), config.lua(), loginCommand(program), instead);
        // With a wallpaper program, Hyprland's own wallpaper and logo only show through when
        // the program changes images. Set where the config writes them; left alone otherwise.
        if (config.lua()) {
            for (const auto& [path, value] : {std::pair{std::vector<std::string_view>{"misc", "force_default_wallpaper"}, std::string_view{"0"}},
                                              std::pair{std::vector<std::string_view>{"misc", "disable_hyprland_logo"}, std::string_view{"true"}}})
                if (auto edited = configuration::withLuaConfigValue(current->proposed, path, value))
                    current->proposed = std::move(*edited);
            if (workspaces.empty()) {
                current->proposed = configuration::withLuaBlock(current->proposed, workspaceBlock, "", false);
            } else {
                const auto block = workspaceWallpaperBlock(program, wallpaper, workspaces);
                if (!block)
                    return std::unexpected{block.error()};
                current->proposed = configuration::withLuaBlock(current->proposed, workspaceBlock, *block, true);
            }
        } else if (!workspaces.empty()) {
            return std::unexpected{"a wallpaper for each workspace needs a Lua config, hyprland.lua"};
        }
        if (current->changes())
            changes.push_back(std::move(*current));
        return changes;
    }

    std::optional<Wallpaper> currentWallpaper(const foundation::Environment& environment) {
        const auto text = readFile(hyprpaperConfigFile(environment));
        return text ? fallbackWallpaper(*text, homeOf(environment)) : std::nullopt;
    }

    std::vector<std::filesystem::path> findWallpapers(const foundation::Environment& environment, std::span<const std::filesystem::path> systemFolders) {
        const auto                         home = homeOf(environment);
        std::vector<std::filesystem::path> found;
        std::set<std::filesystem::path>    seen;
        const auto                         look = [&found, &seen](const std::filesystem::path& folder, bool deeper) {
            std::error_code                    error;
            std::vector<std::filesystem::path> here;
            std::vector<std::filesystem::path> inside;
            for (const auto& entry : std::filesystem::directory_iterator{folder, error}) {
                if (entry.is_directory(error))
                    inside.push_back(entry.path());
                else if (entry.is_regular_file(error) && isImage(entry.path())) {
                    // Hyprland's folder also holds hyprlock's pictures; only its walls are wallpapers.
                    if (folder.filename() == "hypr" && !entry.path().filename().string().starts_with("wall"))
                        continue;
                    here.push_back(entry.path());
                }
            }
            std::ranges::sort(here);
            for (auto& file : here)
                if (seen.insert(file).second)
                    found.push_back(std::move(file));
            if (deeper) {
                std::ranges::sort(inside);
                for (const auto& folderInside : inside) {
                    std::vector<std::filesystem::path> images;
                    for (const auto& entry : std::filesystem::directory_iterator{folderInside, error})
                        if (entry.is_regular_file(error) && isImage(entry.path()))
                            images.push_back(entry.path());
                    std::ranges::sort(images);
                    for (auto& file : images)
                        if (seen.insert(file).second)
                            found.push_back(std::move(file));
                }
            }
        };
        if (!home.empty()) {
            const auto folder = wallpaperFolder(environment);
            look(folder, true);
            look(home / ".local" / "share" / "backgrounds", true);
        }
        for (const auto& folder : systemFolders)
            look(folder, true);
        return found;
    }

    std::filesystem::path wallpaperFolder(const foundation::Environment& environment) {
        const auto home = homeOf(environment);
        auto       pictures = home / "Pictures";
        const auto dirs     = foundation::variable(environment, "XDG_CONFIG_HOME").value_or((home / ".config").string());
        if (const auto text = readFile(std::filesystem::path{dirs} / "user-dirs.dirs")) {
            std::string_view rest = *text;
            while (!rest.empty()) {
                const auto end  = rest.find('\n');
                const auto line = trim(rest.substr(0, end));
                rest            = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
                if (!line.starts_with("XDG_PICTURES_DIR="))
                    continue;
                auto value = line.substr(std::string_view{"XDG_PICTURES_DIR="}.size());
                if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                    value = value.substr(1, value.size() - 2);
                if (value.starts_with("$HOME/"))
                    pictures = home / std::string{value.substr(6)};
                else if (value.starts_with("/"))
                    pictures = std::string{value};
            }
        }
        return pictures / "Wallpapers";
    }

    std::expected<std::filesystem::path, std::string> addWallpaper(const std::filesystem::path& image, const std::filesystem::path& folder) {
        std::error_code error;
        if (image.parent_path() == folder)
            return image;
        std::filesystem::create_directories(folder, error);
        if (error)
            return std::unexpected{std::format("cannot create {}: {}", folder.string(), error.message())};
        const auto source = readFile(image);
        if (!source)
            return std::unexpected{std::format("cannot read {}", image.string())};
        for (const auto& entry : std::filesystem::directory_iterator{folder, error})
            if (entry.is_regular_file(error) && entry.file_size(error) == source->size() && readFile(entry.path()) == source)
                return entry.path();

        auto target = folder / (randomUuid() + image.extension().string());
        while (std::filesystem::exists(target, error))
            target = folder / (randomUuid() + image.extension().string());
        std::filesystem::copy_file(image, target, error);
        if (error)
            return std::unexpected{std::format("cannot copy {} to {}: {}", image.string(), target.string(), error.message())};
        return target;
    }

    std::optional<std::filesystem::path> shownWallpaperImage(const foundation::Environment& environment) {
        if (const auto awww = findProgram("awww", foundation::variable(environment, "PATH").value_or("")))
            if (const auto reply = readCommand(*awww, {"query", "-j"}))
                if (auto image = awwwImage(*reply))
                    return image;
        if (const auto wallpaper = currentWallpaper(environment))
            return wallpaper->image;
        return std::nullopt;
    }

}
