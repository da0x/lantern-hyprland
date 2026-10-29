// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/lantern-folder.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

TEST(Folder, MovesTheFolderKeptBeforeTheRename) {
    const auto root = std::filesystem::temp_directory_path() / ("lantern-hyprland-folder-" + std::to_string(::getpid()));
    std::filesystem::create_directories(root / "hyprland-studio/backups");
    std::ofstream{root / "hyprland-studio/settings.json"} << "{}\n";
    const foundation::Environment environment{{"XDG_CONFIG_HOME", root.string()}};

    application::moveOldFolder(environment);
    EXPECT_FALSE(std::filesystem::exists(root / "hyprland-studio"));
    EXPECT_TRUE(std::filesystem::exists(root / "lantern-hyprland/settings.json"));
    EXPECT_TRUE(std::filesystem::is_directory(root / "lantern-hyprland/backups"));

    // A folder already there is never replaced.
    std::filesystem::create_directories(root / "hyprland-studio");
    application::moveOldFolder(environment);
    EXPECT_TRUE(std::filesystem::exists(root / "hyprland-studio"));
    EXPECT_TRUE(std::filesystem::exists(root / "lantern-hyprland/settings.json"));
    std::filesystem::remove_all(root);
}
