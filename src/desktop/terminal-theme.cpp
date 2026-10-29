// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/terminal-theme.hpp"

#include "configuration/hyprlang-edit.hpp"
#include "configuration/lua-edit.hpp"
#include "desktop/color.hpp"
#include "desktop/system.hpp"

#include <algorithm>

#include <charconv>
#include <format>
#include <fstream>
#include <map>
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

        // Perceived lightness of #rrggbb, from 0 to 1.
        double lightness(std::string_view hex) {
            unsigned value = 0;
            if (hex.size() == 7)
                std::from_chars(hex.data() + 1, hex.data() + 7, value, 16);
            const double red = (value >> 16U) & 0xFFU, green = (value >> 8U) & 0xFFU, blue = value & 0xFFU;
            return (0.299 * red + 0.587 * green + 0.114 * blue) / 255.0;
        }

    }

    TerminalTheme terminalTheme(std::string_view name, std::string_view file) {
        TerminalTheme theme;
        theme.name = name;
        theme.file = file;
        std::size_t at = 0;
        while (at < file.size()) {
            const auto end  = std::min(file.find('\n', at), file.size());
            const auto line = trim(file.substr(at, end - at));
            at              = end + 1;
            if (line.empty() || line.front() == '#')
                continue;
            const auto space = line.find_first_of(" \t");
            if (space == std::string_view::npos)
                continue;
            const auto key   = line.substr(0, space);
            const auto value = std::string{trim(line.substr(space))};
            if (key == "background")
                theme.background = value;
            else if (key == "foreground")
                theme.foreground = value;
            else if (key == "cursor")
                theme.cursor = value;
            else if (key.starts_with("color")) {
                unsigned index = 0;
                const auto [stop, failed] = std::from_chars(key.data() + 5, key.data() + key.size(), index);
                if (failed == std::errc{} && stop == key.data() + key.size() && index < theme.colors.size())
                    theme.colors[index] = value;
            }
        }
        if (theme.cursor.empty() || theme.cursor == "none")
            theme.cursor = theme.foreground;
        theme.dark = lightness(theme.background) < 0.5;
        return theme;
    }

    TerminalTheme adjusted(const TerminalTheme& theme, Contrast contrast, Vision vision) {
        if (contrast == Contrast::standard && vision == Vision::typical)
            return theme;
        TerminalTheme result = theme;

        // Color vision first: the six hues of the 16 colors move, keeping each color's
        // own lightness and chroma, so the theme keeps its feel. The hues, in OKLCH degrees,
        // follow the colors used by palettes made for these kinds of color blindness.
        if (vision != Vision::typical) {
            // red, green, yellow, blue, magenta, cyan
            constexpr std::array<double, 6> redGreen{55, 245, 100, 280, 330, 200};
            constexpr std::array<double, 6> blueYellow{25, 165, 345, 200, 300, 120};
            const auto&                     hues = vision == Vision::redGreen ? redGreen : blueYellow;
            for (std::size_t slot = 0; slot < 6; ++slot)
                for (const auto index : {slot + 1, slot + 9}) {
                    auto lch                = lchOf(parseColor(result.colors[index]));
                    lch.hue                 = hues[slot];
                    result.colors[index]   = hexOf(colorOf(lch));
                }
        }

        if (contrast == Contrast::high) {
            // The background a little further from the middle, then everything read against it.
            auto background = lchOf(parseColor(result.background));
            background.lightness = theme.dark ? background.lightness * 0.7 : background.lightness + (1 - background.lightness) * 0.6;
            const auto ground    = parseColor(hexOf(colorOf(background)));
            result.background    = hexOf(ground);
            result.foreground    = hexOf(withContrast(parseColor(result.foreground), ground, 10.0));
            for (std::size_t index = 1; index < result.colors.size(); ++index)
                if (index != 8)
                    result.colors[index] = hexOf(withContrast(parseColor(result.colors[index]), ground, 7.0));
            result.colors[8] = hexOf(withContrast(parseColor(result.colors[8]), ground, 4.5));
            result.cursor     = hexOf(withContrast(parseColor(result.cursor), ground, 4.5));
        }

        // The file: each value that changed, replaced where the theme sets it.
        auto file = std::string{theme.file};
        const auto set = [&file](std::string_view key, const std::string& before, const std::string& after) {
            if (before != after)
                file = configuration::withSpacedValue(file, key, after);
        };
        set("background", theme.background, result.background);
        set("foreground", theme.foreground, result.foreground);
        set("cursor", theme.cursor, result.cursor);
        for (std::size_t index = 0; index < result.colors.size(); ++index)
            set(std::format("color{}", index), theme.colors[index], result.colors[index]);
        result.contrast = contrast;
        result.vision   = vision;
        result.file     = std::format("# {}, {}\n{}", theme.name, adjustmentName(contrast, vision), file);
        return result;
    }

    std::string adjustmentName(Contrast contrast, Vision vision) {
        std::vector<std::string_view> parts;
        if (contrast == Contrast::high)
            parts.emplace_back("high contrast");
        if (vision == Vision::redGreen)
            parts.emplace_back("for red-green color blindness");
        if (vision == Vision::blueYellow)
            parts.emplace_back("for blue-yellow color blindness");
        std::string name;
        for (const auto part : parts)
            name += std::format("{}{}", name.empty() ? "" : ", ", part);
        return name;
    }

    std::span<const TerminalTheme> terminalThemes(Contrast contrast, Vision vision) {
        if (contrast == Contrast::standard && vision == Vision::typical)
            return terminalThemes();
        // Read and written from the interface's thread only.
        static std::map<std::pair<Contrast, Vision>, std::vector<TerminalTheme>> made;
        auto& themes = made[{contrast, vision}];
        if (themes.empty())
            for (const auto& theme : terminalThemes())
                themes.push_back(adjusted(theme, contrast, vision));
        return themes;
    }

    std::filesystem::path kittyFolder(const foundation::Environment& environment) {
        if (const auto home = foundation::variable(environment, "XDG_CONFIG_HOME"); home && std::filesystem::path{*home}.is_absolute())
            return std::filesystem::path{*home} / "kitty";
        return std::filesystem::path{foundation::variable(environment, "HOME").value_or("")} / ".config" / "kitty";
    }

    std::vector<TerminalFont> terminalFonts() {
        // Open-source monospace fonts in Arch's own repositories.
        std::vector<TerminalFont> fonts{
            {"JetBrains Mono", "ttf-jetbrains-mono", "SIL Open Font License"}, {"Fira Code", "ttf-fira-code", "SIL Open Font License"},
            {"Cascadia Code", "ttf-cascadia-code", "SIL Open Font License"},   {"Source Code Pro", "adobe-source-code-pro-fonts", "SIL Open Font License"},
            {"IBM Plex Mono", "ttf-ibm-plex", "SIL Open Font License"},        {"Iosevka", "ttc-iosevka", "SIL Open Font License"},
            {"Inconsolata", "ttf-inconsolata", "SIL Open Font License"},       {"Monaspace Neon", "otf-monaspace", "SIL Open Font License"},
            {"Hack", "ttf-hack", "MIT"},                                       {"Roboto Mono", "ttf-roboto-mono", "Apache 2.0"},
            {"DejaVu Sans Mono", "ttf-dejavu", "free (Bitstream Vera)"},       {"Noto Sans Mono", "noto-fonts", "SIL Open Font License"},
        };
        const auto installed = monospaceFamilies();
        for (auto& font : fonts)
            font.installed = std::ranges::contains(installed, font.family);
        for (const auto& family : installed)
            if (std::ranges::find(fonts, family, &TerminalFont::family) == fonts.end())
                fonts.push_back({family, "", "", true});
        return fonts;
    }

    std::optional<std::string> kittyFontFamily(const foundation::Environment& environment) {
        std::ifstream     stream{kittyFolder(environment) / "kitty.conf"};
        std::stringstream text;
        text << stream.rdbuf();
        return configuration::spacedValue(text.str(), "font_family");
    }

    double kittyFontSize(const foundation::Environment& environment) {
        std::ifstream     stream{kittyFolder(environment) / "kitty.conf"};
        std::stringstream text;
        text << stream.rdbuf();
        if (const auto value = configuration::spacedValue(text.str(), "font_size")) {
            double size = 0;
            if (const auto [end, failed] = std::from_chars(value->data(), value->data() + value->size(), size); failed == std::errc{} && size > 0)
                return size;
        }
        return 11.0;
    }

    namespace {

        constexpr double clearOpacity = 0.5;

        constexpr std::string_view sharpRule = "hl.window_rule({\n"
                                               "    -- The terminal at Lantern's Clear, with what is behind it left sharp.\n"
                                               "    name    = \"clear-terminal\",\n"
                                               "    match   = { class = \"^kitty\" },\n"
                                               "\n"
                                               "    no_blur = true,\n"
                                               "})\n";

    }

    std::span<const TerminalOpacity> terminalOpacities() {
        static constexpr std::array opacities{
            TerminalOpacity{.value = 1.0, .name = "Solid"},
            TerminalOpacity{.value = 0.95, .name = "Slight"},
            TerminalOpacity{.value = 0.85, .name = "Glass"},
            TerminalOpacity{.value = clearOpacity, .name = "Clear"},
        };
        return opacities;
    }

    double kittyOpacity(const foundation::Environment& environment) {
        std::ifstream     stream{kittyFolder(environment) / "kitty.conf"};
        std::stringstream text;
        text << stream.rdbuf();
        if (const auto value = configuration::spacedValue(text.str(), "background_opacity")) {
            double opacity = 0;
            if (const auto [end, failed] = std::from_chars(value->data(), value->data() + value->size(), opacity); failed == std::errc{} && opacity >= 0 && opacity <= 1)
                return opacity;
        }
        return 1.0;
    }

    bool kittyDynamicOpacity(const foundation::Environment& environment) {
        std::ifstream     stream{kittyFolder(environment) / "kitty.conf"};
        std::stringstream text;
        text << stream.rdbuf();
        return configuration::spacedValue(text.str(), "dynamic_background_opacity") == "yes";
    }

    std::expected<std::vector<configuration::FileChange>, std::string> planTerminal(const TerminalTheme& theme, double fontSize, const std::optional<std::string>& fontFamily,
                                                                                   double opacity, const std::optional<configuration::MainConfig>& hyprland,
                                                                                   const foundation::Environment& environment) {
        std::vector<configuration::FileChange> changes;
        if (!findProgram("kitty", foundation::variable(environment, "PATH").value_or("")))
            return changes;
        const auto folder = kittyFolder(environment);
        // kitty's own convention, as `kitten themes` follows it: the colors in
        // current-theme.conf, which kitty.conf includes.
        for (auto planned : {configuration::planEdit(folder / "current-theme.conf", [&theme](std::string_view) { return theme.file; }),
                             configuration::planEdit(folder / "kitty.conf", [fontSize, &fontFamily, opacity, &environment](std::string_view text) {
                                 std::string result{text};
                                 if (!configuration::hasLine(result, "include current-theme.conf")) {
                                     if (!result.empty() && !result.ends_with('\n'))
                                         result += '\n';
                                     result += "include current-theme.conf\n";
                                 }
                                 // Written only when it changes, so a config without one keeps kitty's default.
                                 if (fontSize != kittyFontSize(environment))
                                     result = configuration::withSpacedValue(result, "font_size", std::format("{:g}", fontSize));
                                 if (fontFamily && fontFamily != kittyFontFamily(environment))
                                     result = configuration::withSpacedValue(result, "font_family", *fontFamily);
                                 if (opacity != kittyOpacity(environment))
                                     result = configuration::withSpacedValue(result, "background_opacity", std::format("{:g}", opacity));
                                 if (opacity < 1 && configuration::spacedValue(result, "dynamic_background_opacity") != "yes")
                                     result = configuration::withSpacedValue(result, "dynamic_background_opacity", "yes");
                                 return result;
                             })}) {
            if (!planned)
                return std::unexpected{planned.error()};
            if (*planned)
                changes.push_back(std::move(**planned));
        }
        if (hyprland && hyprland->lua()) {
            auto sharp = configuration::planEdit(
                hyprland->file, [opacity](std::string_view text) { return configuration::withLuaWindowRule(text, "clear-terminal", sharpRule, opacity <= clearOpacity); });
            if (!sharp)
                return std::unexpected{sharp.error()};
            if (*sharp)
                changes.push_back(std::move(**sharp));
        }
        return changes;
    }

    const TerminalTheme* currentTerminal(const foundation::Environment& environment) {
        std::ifstream     stream{kittyFolder(environment) / "current-theme.conf", std::ios::binary};
        std::stringstream text;
        text << stream.rdbuf();
        for (const auto contrast : {Contrast::standard, Contrast::high})
            for (const auto vision : {Vision::typical, Vision::redGreen, Vision::blueYellow})
                for (const auto& theme : terminalThemes(contrast, vision))
                    if (text.str() == theme.file)
                        return &theme;
        return nullptr;
    }

}
