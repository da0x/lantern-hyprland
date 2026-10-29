// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/main-config.hpp"
#include "desktop/cursor-theme.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

    void little(std::string& out, std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8)
            out.push_back(static_cast<char>((value >> static_cast<unsigned>(shift)) & 0xFFU));
    }

    // An XCursor file with one image, `side` pixels square, every pixel half-transparent red.
    std::string xcursor(std::uint32_t side) {
        std::string file = "Xcur";
        little(file, 16);
        little(file, 0x10000);
        little(file, 1);
        little(file, 0xFFFD0002U);
        little(file, side);
        little(file, 28);
        for (const std::uint32_t field : {36U, 0xFFFD0002U, side, 1U, side, side, 0U, 0U, 0U})
            little(file, field);
        for (std::uint32_t pixel = 0; pixel < side * side; ++pixel)
            little(file, 0x80800000U);
        return file;
    }

}

TEST(Cursor, ReadsThemesAndDrawsTheirCursors) {
    const auto root = std::filesystem::temp_directory_path() / ("lantern-hyprland-cursor-" + std::to_string(::getpid()));
    std::filesystem::create_directories(root / "share/icons/Pointy/cursors");
    std::ofstream{root / "share/icons/Pointy/index.theme"} << "[Icon Theme]\nName = Pointy Things\n";
    std::ofstream{root / "share/icons/Pointy/cursors/left_ptr", std::ios::binary} << xcursor(4);
    const foundation::Environment environment{{"HOME", (root / "home").string()}, {"XDG_DATA_DIRS", (root / "share").string()}};

    const auto themes = desktop::cursorThemes(environment);
    ASSERT_FALSE(themes.empty());
    EXPECT_EQ(themes.front().name, "Pointy");
    EXPECT_EQ(themes.front().title, "Pointy Things");
    // The recommended ones follow, not installed, with their package.
    const auto breeze = std::ranges::find(themes, std::string{"breeze_cursors"}, &desktop::CursorTheme::name);
    ASSERT_NE(breeze, themes.end());
    EXPECT_TRUE(breeze->folder.empty());
    EXPECT_EQ(breeze->package, "breeze-cursors");

    const auto image = desktop::cursorImage(themes.front().folder, {"default", "left_ptr"}, 24);
    ASSERT_TRUE(image.has_value());
    ASSERT_GT(image->size(), 33U);
    EXPECT_EQ(std::string(image->begin() + 1, image->begin() + 4), "PNG");
    EXPECT_EQ((*image)[19], 4);
    EXPECT_FALSE(desktop::cursorImage(themes.front().folder, {"watch"}, 24).has_value());

    // The Hyprland config names the theme, and X11 apps inherit it.
    std::filesystem::create_directories(root / "home/.config/hypr");
    std::ofstream{root / "home/.config/hypr/hyprland.lua"} << "hl.env(\"XCURSOR_SIZE\", \"32\")\n";
    const configuration::MainConfig config{.file = root / "home/.config/hypr/hyprland.lua", .choice = configuration::Choice::search, .safeMode = false};
    const auto                      planned = desktop::planCursorTheme(themes.front(), config, environment);
    ASSERT_TRUE(planned.has_value()) << planned.error();
    ASSERT_EQ(planned->size(), 2U);
    EXPECT_TRUE((*planned)[0].proposed.contains("hl.env(\"XCURSOR_THEME\", \"Pointy\")"));
    EXPECT_EQ((*planned)[1].proposed, "[Icon Theme]\nInherits=Pointy\n");
    std::filesystem::remove_all(root);
}
