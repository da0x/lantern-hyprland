// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/file-change.hpp"
#include "configuration/lua-edit.hpp"
#include "desktop/color.hpp"
#include "desktop/terminal-theme.hpp"
#include "desktop/theme.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <format>
#include <string>

#include <unistd.h>

namespace {

    TEST(Theme, PlansBordersWaybarAndHyprtoolkitAndRecognizesTheResult) {
        static std::atomic<int> counter = 0;
        const auto root = std::filesystem::temp_directory_path() / ("lantern-hyprland-theme-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
        std::filesystem::create_directories(root / "home/.config/hypr");
        std::ofstream{root / "home/.config/hypr/hyprland.lua"} << "hl.config({\n    general = {\n        col = {\n            active_border = \"rgba(33ccffee)\",\n"
                                                                 "            inactive_border = \"rgba(595959aa)\",\n        },\n    },\n})\n";
        std::ofstream{root / "home/.config/hypr/hyprtoolkit.conf"} << "# my toolkit\nfont_size = 12\n";
        const foundation::Environment  environment{{"HOME", (root / "home").string()}};
        const configuration::MainConfig config{.file = root / "home/.config/hypr/hyprland.lua", .choice = configuration::Choice::search, .safeMode = false};
        const auto&                    tokyo = desktop::waybarPalettes()[3];

        EXPECT_EQ(desktop::currentTheme(config), nullptr);
        const auto planned = desktop::planTheme(tokyo, desktop::WaybarStyle{.shape = desktop::Shape::floating}, config, environment);
        ASSERT_TRUE(planned.has_value()) << planned.error();
        // The Hyprland config, Waybar's two files, hyprtoolkit.conf, and GTK's two
        // stylesheets and settings.ini.
        ASSERT_EQ(planned->size(), 7U);
        EXPECT_TRUE((*planned)[0].proposed.contains("active_border = { colors = {\"rgba(7aa2f7ee)\", \"rgba(7dcfffee)\"}, angle = 45 },"));
        EXPECT_TRUE((*planned)[0].proposed.contains("inactive_border = \"rgba(3b4261aa)\","));
        EXPECT_EQ((*planned)[3].proposed.substr(0, 30), "# my toolkit\nfont_size = 12\nba");
        EXPECT_TRUE((*planned)[3].proposed.contains("accent = 0xff7aa2f7\n"));

        ASSERT_TRUE(configuration::applyChanges(*planned, root / "backups", "Theme").has_value());
        EXPECT_EQ(desktop::currentTheme(config), &tokyo);
        const auto again = desktop::planTheme(tokyo, desktop::WaybarStyle{.shape = desktop::Shape::floating}, config, environment);
        ASSERT_TRUE(again.has_value());
        EXPECT_TRUE(again->empty());
        std::filesystem::remove_all(root);
    }

    TEST(Theme, ThemesKittyWofiAndDunstWhenInstalled) {
        static std::atomic<int> counter = 0;
        const auto root = std::filesystem::temp_directory_path() / ("lantern-hyprland-theme-apps-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
        std::filesystem::create_directories(root / "bin");
        for (const auto* program : {"kitty", "wofi", "dunst"}) {
            std::ofstream{root / "bin" / program} << "#!/bin/sh\n";
            std::filesystem::permissions(root / "bin" / program, std::filesystem::perms::owner_all);
        }
        std::filesystem::create_directories(root / "home/.config/hypr");
        std::filesystem::create_directories(root / "home/.config/kitty");
        std::ofstream{root / "home/.config/kitty/kitty.conf"} << "font_size 12\n";
        std::filesystem::create_directories(root / "home/.config/dunst");
        std::ofstream{root / "home/.config/dunst/dunstrc"} << "[global]\n    frame_color = \"#aaaaaa\"\n";
        std::ofstream{root / "home/.config/hypr/hyprland.lua"} << "hl.config({ general = { col = { active_border = \"x\", inactive_border = \"y\" } } })\n";
        const foundation::Environment  environment{{"HOME", (root / "home").string()}, {"PATH", (root / "bin").string()}};
        const configuration::MainConfig config{.file = root / "home/.config/hypr/hyprland.lua", .choice = configuration::Choice::search, .safeMode = false};

        const auto& nord    = desktop::waybarPalettes()[4];
        const auto  planned = desktop::planTheme(nord, desktop::WaybarStyle{.shape = desktop::Shape::islands}, config, environment);
        ASSERT_TRUE(planned.has_value()) << planned.error();
        const auto find = [&planned](const std::string& name) -> const configuration::FileChange* {
            for (const auto& change : *planned)
                if (change.file.filename() == name)
                    return &change;
            return nullptr;
        };
        ASSERT_NE(find("style.css"), nullptr);
        ASSERT_NE(find("dunstrc"), nullptr);
        EXPECT_TRUE(find("dunstrc")->proposed.starts_with("[global]\n    frame_color = \"#88c0d0\"\n"));
        EXPECT_TRUE(find("dunstrc")->proposed.contains("[urgency_critical]\n    background = \"#2e3440\"\n"));
        // GTK apps get the palette, GTK 3 its dark preference too.
        ASSERT_NE(find("gtk.css"), nullptr);
        EXPECT_TRUE(find("gtk.css")->proposed.contains("@define-color accent    #88c0d0;"));
        ASSERT_NE(find("settings.ini"), nullptr);
        EXPECT_TRUE(find("settings.ini")->proposed.contains("gtk-application-prefer-dark-theme = 1"));
        // The terminal has themes of its own.
        EXPECT_EQ(find("current-theme.conf"), nullptr);
        EXPECT_EQ(find("kitty.conf"), nullptr);
        std::filesystem::remove_all(root);
    }

    TEST(Terminal, BundlesDarkAndLightThemesReadFromTheirFiles) {
        const auto themes = desktop::terminalThemes();
        ASSERT_EQ(themes.size(), 35U);
        const auto vivendi = std::ranges::find(themes, std::string{"Modus Vivendi"}, &desktop::TerminalTheme::name);
        ASSERT_NE(vivendi, themes.end());
        EXPECT_EQ(vivendi->background, "#000000");
        const auto dracula = std::ranges::find(themes, std::string{"Dracula"}, &desktop::TerminalTheme::name);
        ASSERT_NE(dracula, themes.end());
        EXPECT_EQ(dracula->background, "#282a36");
        EXPECT_EQ(dracula->colors[1], "#ff5555");
        EXPECT_TRUE(dracula->dark);
        const auto latte = std::ranges::find(themes, std::string{"Catppuccin Latte"}, &desktop::TerminalTheme::name);
        ASSERT_NE(latte, themes.end());
        EXPECT_FALSE(latte->dark);
        for (const auto& theme : themes)
            for (const auto& color : theme.colors)
                EXPECT_EQ(color.size(), 7U) << theme.name;
    }

    TEST(Terminal, WritesTheThemeFileAsKittyDoesAndRecognizesIt) {
        static std::atomic<int> counter = 0;
        const auto root = std::filesystem::temp_directory_path() / ("lantern-hyprland-terminal-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
        std::filesystem::create_directories(root / "bin");
        std::ofstream{root / "bin" / "kitty"} << "#!/bin/sh\n";
        std::filesystem::permissions(root / "bin" / "kitty", std::filesystem::perms::owner_all);
        const foundation::Environment environment{{"HOME", (root / "home").string()}, {"PATH", (root / "bin").string()}};
        const auto&                   nord = *std::ranges::find(desktop::terminalThemes(), std::string{"Nord"}, &desktop::TerminalTheme::name);

        EXPECT_EQ(desktop::currentTerminal(environment), nullptr);
        const auto planned = desktop::planTerminal(nord, 11.0, std::nullopt, 1.0, std::nullopt, environment);
        ASSERT_TRUE(planned.has_value()) << planned.error();
        ASSERT_EQ(planned->size(), 2U);
        EXPECT_EQ((*planned)[0].proposed, nord.file);
        EXPECT_EQ((*planned)[1].proposed, "include current-theme.conf\n");
        ASSERT_TRUE(configuration::applyChanges(*planned, root / "backups", "Terminal").has_value());
        EXPECT_EQ(desktop::currentTerminal(environment), &nord);
        const auto again = desktop::planTerminal(nord, 11.0, std::nullopt, 1.0, std::nullopt, environment);
        ASSERT_TRUE(again.has_value());
        EXPECT_TRUE(again->empty());

        // A bigger font: only font_size is added to kitty.conf, and then read back.
        EXPECT_EQ(desktop::kittyFontSize(environment), 11.0);
        const auto bigger = desktop::planTerminal(nord, 13.0, std::nullopt, 1.0, std::nullopt, environment);
        ASSERT_TRUE(bigger.has_value());
        ASSERT_EQ(bigger->size(), 1U);
        EXPECT_EQ((*bigger)[0].proposed, "include current-theme.conf\nfont_size 13\n");
        ASSERT_TRUE(configuration::applyChanges(*bigger, root / "backups", "Terminal").has_value());
        EXPECT_EQ(desktop::kittyFontSize(environment), 13.0);

        // A font family: font_family is set, and read back.
        EXPECT_FALSE(desktop::kittyFontFamily(environment).has_value());
        const auto family = desktop::planTerminal(nord, 13.0, std::string{"JetBrains Mono"}, 1.0, std::nullopt, environment);
        ASSERT_TRUE(family.has_value());
        ASSERT_EQ(family->size(), 1U);
        EXPECT_EQ((*family)[0].proposed, "include current-theme.conf\nfont_size 13\nfont_family JetBrains Mono\n");
        ASSERT_TRUE(configuration::applyChanges(*family, root / "backups", "Terminal").has_value());
        EXPECT_EQ(desktop::kittyFontFamily(environment), std::optional<std::string>{"JetBrains Mono"});

        // A see-through background: background_opacity, and the dynamic opacity a running
        // kitty needs to take it on reload, each written once.
        EXPECT_EQ(desktop::kittyOpacity(environment), 1.0);
        const auto glass = desktop::planTerminal(nord, 13.0, std::string{"JetBrains Mono"}, 0.85, std::nullopt, environment);
        ASSERT_TRUE(glass.has_value());
        ASSERT_EQ(glass->size(), 1U);
        EXPECT_EQ((*glass)[0].proposed, "include current-theme.conf\nfont_size 13\nfont_family JetBrains Mono\nbackground_opacity 0.85\ndynamic_background_opacity yes\n");
        ASSERT_TRUE(configuration::applyChanges(*glass, root / "backups", "Terminal").has_value());
        EXPECT_EQ(desktop::kittyOpacity(environment), 0.85);
        const auto solid = desktop::planTerminal(nord, 13.0, std::string{"JetBrains Mono"}, 1.0, std::nullopt, environment);
        ASSERT_TRUE(solid.has_value());
        ASSERT_EQ(solid->size(), 1U);
        EXPECT_EQ((*solid)[0].proposed, "include current-theme.conf\nfont_size 13\nfont_family JetBrains Mono\nbackground_opacity 1\ndynamic_background_opacity yes\n");
        ASSERT_TRUE(configuration::applyChanges(*solid, root / "backups", "Terminal").has_value());

        // An adjusted theme is recognized as itself, from the same set each time.
        const auto  index = static_cast<std::size_t>(&nord - desktop::terminalThemes().data());
        const auto& high  = desktop::terminalThemes(desktop::Contrast::high, desktop::Vision::blueYellow)[index];
        const auto  adjusted = desktop::planTerminal(high, 13.0, std::string{"JetBrains Mono"}, 1.0, std::nullopt, environment);
        ASSERT_TRUE(adjusted.has_value());
        ASSERT_TRUE(configuration::applyChanges(*adjusted, root / "backups", "Terminal").has_value());
        EXPECT_EQ(desktop::currentTerminal(environment), &high);

        // Clear leaves the terminal unblurred with a window rule of Lantern's, which Glass
        // takes away again with the blank line it came with.
        const configuration::MainConfig hyprland{.file = root / "hyprland.lua"};
        const std::string               config = "hl.window_rule({\n    name  = \"other\",\n    match = { class = \"x\" },\n})\n";
        std::ofstream{hyprland.file} << config;
        const auto clear = desktop::planTerminal(high, 13.0, std::string{"JetBrains Mono"}, 0.5, hyprland, environment);
        ASSERT_TRUE(clear.has_value()) << clear.error();
        ASSERT_EQ(clear->size(), 2U);
        EXPECT_EQ((*clear)[1].file, hyprland.file);
        EXPECT_TRUE((*clear)[1].proposed.starts_with(config + "\nhl.window_rule({\n"));
        EXPECT_TRUE(configuration::hasLuaWindowRule((*clear)[1].proposed, "clear-terminal"));
        EXPECT_TRUE((*clear)[1].proposed.contains("    no_blur = true,\n"));
        ASSERT_TRUE(configuration::applyChanges(*clear, root / "backups", "Terminal").has_value());
        const auto glassAgain = desktop::planTerminal(high, 13.0, std::string{"JetBrains Mono"}, 0.85, hyprland, environment);
        ASSERT_TRUE(glassAgain.has_value());
        ASSERT_EQ(glassAgain->size(), 2U);
        EXPECT_EQ((*glassAgain)[1].proposed, config);
        std::filesystem::remove_all(root);
    }

    TEST(Terminal, BundledThemesSetColorsOnly) {
        // A theme that set opacity or fonts would change kitty beyond its colors.
        for (const auto& theme : desktop::terminalThemes()) {
            std::size_t at = 0;
            while (at < theme.file.size()) {
                const auto end  = std::min(theme.file.find('\n', at), theme.file.size());
                const auto line = std::string_view{theme.file}.substr(at, end - at);
                at              = end + 1;
                const auto key  = line.substr(0, line.find_first_of(" \t"));
                if (key.empty() || key.front() == '#')
                    continue;
                const bool color = key.starts_with("color") || key.ends_with("_color") || key.ends_with("_foreground") || key.ends_with("_background") ||
                                    key == "foreground" || key == "background" || key == "cursor";
                EXPECT_TRUE(color) << theme.name << " sets " << key;
            }
        }
    }

    TEST(Terminal, RecommendsOpenSourceFontsFirst) {
        const auto fonts = desktop::terminalFonts();
        ASSERT_GE(fonts.size(), 12U);
        EXPECT_EQ(fonts.front().family, "JetBrains Mono");
        EXPECT_EQ(fonts.front().package, "ttf-jetbrains-mono");
        for (std::size_t index = 12; index < fonts.size(); ++index)
            EXPECT_TRUE(fonts[index].installed) << fonts[index].family;
    }

    TEST(Terminal, MakesEveryThemeHighContrast) {
        for (const auto& theme : desktop::terminalThemes()) {
            const auto high       = desktop::adjusted(theme, desktop::Contrast::high, desktop::Vision::typical);
            const auto background = desktop::parseColor(high.background);
            EXPECT_GE(desktop::contrast(desktop::parseColor(high.foreground), background), 9.99) << theme.name;
            for (std::size_t index = 1; index < 16; ++index) {
                if (index == 8)
                    continue;
                EXPECT_GE(desktop::contrast(desktop::parseColor(high.colors[index]), background), 6.99) << theme.name << " color" << index;
            }
            EXPECT_GE(desktop::contrast(desktop::parseColor(high.colors[8]), background), 4.49) << theme.name;
            // What kitty reads is what was measured.
            const auto reread = desktop::terminalTheme(high.name, high.file);
            EXPECT_EQ(reread.foreground, high.foreground);
            EXPECT_EQ(reread.background, high.background);
            EXPECT_EQ(reread.colors, high.colors);
        }
    }

    TEST(Terminal, MovesRedAndGreenForRedGreenColorBlindness) {
        const auto& dracula = *std::ranges::find(desktop::terminalThemes(), std::string{"Dracula"}, &desktop::TerminalTheme::name);
        const auto  safe    = desktop::adjusted(dracula, desktop::Contrast::standard, desktop::Vision::redGreen);
        EXPECT_NEAR(desktop::lchOf(desktop::parseColor(safe.colors[1])).hue, 55, 3);
        EXPECT_NEAR(desktop::lchOf(desktop::parseColor(safe.colors[2])).hue, 245, 3);
        // Lightness is the theme's own.
        EXPECT_NEAR(desktop::lchOf(desktop::parseColor(safe.colors[1])).lightness, desktop::lchOf(desktop::parseColor(dracula.colors[1])).lightness, 0.01);
        EXPECT_EQ(safe.background, dracula.background);
        EXPECT_EQ(safe.name, "Dracula");
        EXPECT_TRUE(safe.file.starts_with("# Dracula, for red-green color blindness\n"));
        // Untouched when nothing is asked.
        EXPECT_EQ(desktop::adjusted(dracula, desktop::Contrast::standard, desktop::Vision::typical).file, dracula.file);
    }

}
