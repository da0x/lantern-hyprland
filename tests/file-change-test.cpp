// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/diff.hpp"
#include "configuration/file-change.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <unistd.h>

namespace {

    using configuration::DiffLine;

    class Folder {
      public:
        Folder() {
            static std::atomic<int> counter = 0;
            root = std::filesystem::temp_directory_path() / ("lantern-hyprland-file-change-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
            std::filesystem::create_directories(root);
        }
        ~Folder() { std::filesystem::remove_all(root); }
        Folder(const Folder&)            = delete;
        Folder& operator=(const Folder&) = delete;

        std::filesystem::path write(const std::string& name, const std::string& text) const {
            const auto file = root / name;
            std::filesystem::create_directories(file.parent_path());
            std::ofstream{file, std::ios::binary} << text;
            return file;
        }

        std::filesystem::path root;
    };

    std::string read(const std::filesystem::path& file) {
        std::stringstream text;
        text << std::ifstream{file, std::ios::binary}.rdbuf();
        return text.str();
    }

    TEST(Diff, FindsTheLinesThatChanged) {
        const auto lines = configuration::diffLines("a\nb\nc\n", "a\nB\nc\nd\n");
        ASSERT_EQ(lines.size(), 5U);
        EXPECT_EQ(lines[0].kind, DiffLine::Kind::same);
        EXPECT_EQ(lines[1].kind, DiffLine::Kind::removed);
        EXPECT_EQ(lines[1].text, "b");
        EXPECT_EQ(lines[2].kind, DiffLine::Kind::added);
        EXPECT_EQ(lines[2].text, "B");
        EXPECT_EQ(lines[3].kind, DiffLine::Kind::same);
        EXPECT_EQ(lines[4].kind, DiffLine::Kind::added);
        EXPECT_EQ(lines[4].line, 4);
    }

    TEST(Diff, KeepsOnlyTheContextAroundChanges) {
        std::string before;
        for (int line = 1; line <= 20; ++line)
            before += std::to_string(line) + "\n";
        auto after = before;
        after.replace(after.find("2\n"), 2, "two\n");
        after.replace(after.find("18\n"), 3, "eighteen\n");

        const auto kept = configuration::changesOnly(configuration::diffLines(before, after), 1);
        // 1 (2 → two) 3, a gap, 17 (18 → eighteen) 19.
        ASSERT_EQ(kept.size(), 9U);
        EXPECT_EQ(kept[4].line, 0);
        EXPECT_EQ(kept[5].text, "17");
    }

    TEST(FileChange, WritesBacksUpAndUndoes) {
        const Folder folder;
        const auto   existing = folder.write("waybar/style.css", "old style\n");
        const auto   created  = folder.root / "waybar" / "config.jsonc";
        const auto   backups  = folder.root / "backups";

        const auto style  = configuration::planFile(existing, "new style\n");
        const auto config = configuration::planFile(created, "{}\n");
        ASSERT_TRUE(style && config);
        EXPECT_FALSE(config->current.has_value());

        const auto record = configuration::applyChanges({*style, *config}, backups, "Waybar style");
        ASSERT_TRUE(record.has_value()) << record.error();
        EXPECT_EQ(read(existing), "new style\n");
        EXPECT_EQ(read(created), "{}\n");
        EXPECT_EQ(read(*record / "change.txt"), "Waybar style\nreplaced " + existing.string() + "\ncreated " + created.string() + "\n");
        EXPECT_EQ(configuration::latestChange(backups), *record);

        ASSERT_TRUE(configuration::undoChange(*record).has_value());
        EXPECT_EQ(read(existing), "old style\n");
        EXPECT_FALSE(std::filesystem::exists(created));
        EXPECT_FALSE(configuration::latestChange(backups).has_value());
    }

    TEST(FileChange, KeepsAFilesPermissions) {
        const Folder folder;
        const auto   file = folder.write("script", "old\n");
        std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write | std::filesystem::perms::owner_exec);
        const auto change = configuration::planFile(file, "new\n");
        ASSERT_TRUE(change && configuration::applyChanges({*change}, folder.root / "backups", "test"));
        EXPECT_NE(std::filesystem::status(file).permissions() & std::filesystem::perms::owner_exec, std::filesystem::perms::none);
    }

    TEST(FileChange, RefusesAFileChangedAfterReview) {
        const Folder folder;
        const auto   file   = folder.write("style.css", "reviewed\n");
        const auto   change = configuration::planFile(file, "new\n");
        ASSERT_TRUE(change.has_value());
        folder.write("style.css", "edited meanwhile\n");
        EXPECT_FALSE(configuration::applyChanges({*change}, folder.root / "backups", "test").has_value());
        EXPECT_EQ(read(file), "edited meanwhile\n");
    }

    TEST(FileChange, DoesNotUndoOverLaterEdits) {
        const Folder folder;
        const auto   file   = folder.write("style.css", "old\n");
        const auto   change = configuration::planFile(file, "new\n");
        const auto   record = configuration::applyChanges({*change}, folder.root / "backups", "test");
        ASSERT_TRUE(record.has_value());
        folder.write("style.css", "the user's own edit\n");
        EXPECT_FALSE(configuration::undoChange(*record).has_value());
        EXPECT_EQ(read(file), "the user's own edit\n");
    }

    TEST(FileChange, RefusesToReplaceALink) {
        const Folder folder;
        const auto   target = folder.write("dotfiles/style.css", "linked\n");
        const auto   link   = folder.root / "style.css";
        std::filesystem::create_symlink(target, link);
        EXPECT_FALSE(configuration::planFile(link, "new\n").has_value());
    }

}
