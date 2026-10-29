// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/user-themes.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <unistd.h>

namespace {

    TEST(UserThemes, WritesAndReadsATheme) {
        auto draft = desktop::draftOf(desktop::waybarPalettes().front());
        draft.name = "Sunset";
        EXPECT_TRUE(desktop::draftProblems(draft).empty());
        const auto text = desktop::themeFileText(draft);
        EXPECT_TRUE(text.contains("name = Sunset\ndark = true\nbase = #1e1e2e\n"));
        EXPECT_EQ(desktop::parseThemeFile(text), draft);
        EXPECT_FALSE(desktop::parseThemeFile("name = Half\nbase = #000000\n").has_value());

        auto broken   = draft;
        broken.accent = "purple";
        broken.name   = " ";
        EXPECT_EQ(desktop::draftProblems(broken), (std::vector<std::string>{"It needs a name.", "Accent is not a color written as #rrggbb."}));
        auto faint = draft;
        faint.text = faint.base;
        EXPECT_EQ(desktop::draftProblems(faint).size(), 1U);
    }

    TEST(UserThemes, FindsTheNearestGnomeAccent) {
        EXPECT_EQ(desktop::gnomeAccentOf("#cba6f7"), "purple");
        EXPECT_EQ(desktop::gnomeAccentOf("#7aa2f7"), "blue");
        EXPECT_EQ(desktop::gnomeAccentOf("#fabd2f"), "yellow");
        EXPECT_EQ(desktop::gnomeAccentOf("#888888"), "slate");
    }

    TEST(UserThemes, KeepsEachPaletteWhereItIs) {
        const auto folder = std::filesystem::temp_directory_path() / ("lantern-hyprland-themes-" + std::to_string(::getpid()));
        std::filesystem::remove_all(folder);
        std::filesystem::create_directories(folder);
        auto draft = desktop::draftOf(desktop::waybarPalettes()[3]);
        draft.name = "Midnight";
        const auto file = desktop::newThemeFile(folder, "Midnight!");
        EXPECT_EQ(file.filename(), "midnight.conf");
        std::ofstream{file} << desktop::themeFileText(draft);
        std::ofstream{folder / "broken.conf"} << "name = Broken\n";
        EXPECT_EQ(desktop::newThemeFile(folder, "midnight").filename(), "midnight-2.conf");

        const auto builtIn = desktop::waybarPalettes().size();
        desktop::loadUserPalettes(folder);
        auto all = desktop::allPalettes();
        ASSERT_EQ(all.size(), builtIn + 1);
        const auto* midnight = all.back();
        EXPECT_EQ(midnight->name, "Midnight");
        EXPECT_EQ(midnight->gnomeAccent, "blue");
        EXPECT_EQ(desktop::userThemeFile(midnight), file);
        EXPECT_FALSE(desktop::userThemeFile(all.front()).has_value());

        desktop::loadUserPalettes(folder);
        EXPECT_EQ(desktop::allPalettes().back(), midnight);
        draft.accent = "#ff0000";
        std::ofstream{file} << desktop::themeFileText(draft);
        desktop::loadUserPalettes(folder);
        EXPECT_NE(desktop::allPalettes().back(), midnight);
        EXPECT_EQ(midnight->name, "Midnight");

        std::filesystem::remove_all(folder);
        desktop::loadUserPalettes(folder);
        EXPECT_EQ(desktop::allPalettes().size(), builtIn);
    }

}
