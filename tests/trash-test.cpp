// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/trash.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <unistd.h>

namespace {

    std::string read(const std::filesystem::path& file) {
        std::ifstream     stream{file};
        std::stringstream text;
        text << stream.rdbuf();
        return text.str();
    }

    TEST(Trash, MovesAFileThereWithWhereItWas) {
        const auto root = std::filesystem::temp_directory_path() / ("lantern-hyprland-trash-" + std::to_string(::getpid()));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "Pictures");
        const auto file = root / "Pictures" / "screen shot.png";
        std::ofstream{file} << "one";
        const auto when  = std::chrono::system_clock::now();
        const auto moved = desktop::moveToTrash(file, root / "Trash", when);
        ASSERT_TRUE(moved.has_value()) << moved.error();
        EXPECT_EQ(*moved, root / "Trash" / "files" / "screen shot.png");
        EXPECT_FALSE(std::filesystem::exists(file));
        const auto info = read(root / "Trash" / "info" / "screen shot.png.trashinfo");
        EXPECT_TRUE(info.starts_with("[Trash Info]\nPath=" + (root / "Pictures").string() + "/screen%20shot.png\nDeletionDate="));

        // A second of the same name takes another.
        std::ofstream{file} << "two";
        const auto again = desktop::moveToTrash(file, root / "Trash", when);
        ASSERT_TRUE(again.has_value()) << again.error();
        EXPECT_EQ(again->filename(), "screen shot.2.png");
        EXPECT_EQ(read(*again), "two");
        EXPECT_FALSE(desktop::moveToTrash(file, root / "Trash", when).has_value());
        std::filesystem::remove_all(root);
    }

}
