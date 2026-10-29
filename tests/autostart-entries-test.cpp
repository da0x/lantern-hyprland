// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/autostart-entries.hpp"

#include <gtest/gtest.h>

#include <array>
#include <fstream>
#include <unistd.h>

namespace {

    void write(const std::filesystem::path& file, std::string_view text) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream{file} << text;
    }

    TEST(AutostartEntries, ReadsTheFoldersTheUsersFirst) {
        const auto root = std::filesystem::temp_directory_path() / ("lantern-hyprland-autostart-" + std::to_string(::getpid()));
        std::filesystem::remove_all(root);
        write(root / "system/nm-applet.desktop", "[Desktop Entry]\nName=NetworkManager Applet\nExec=nm-applet\nNotShowIn=KDE;GNOME;\n");
        write(root / "system/baloo.desktop", "[Desktop Entry]\nName=Baloo\nExec=baloo_file\nOnlyShowIn=KDE;GNOME;\n");
        write(root / "system/tracker.desktop", "[Desktop Entry]\nName=Tracker\nExec=tracker\nX-GNOME-Autostart-enabled=false\n");
        write(root / "user/nm-applet.desktop", "[Desktop Entry]\nName=NetworkManager Applet\nExec=nm-applet\nHidden=true\n");
        write(root / "user/notes.txt", "not an entry");

        const std::array system{root / "system"};
        const auto       entries = desktop::autostartEntries(root / "user", system, "Hyprland");
        ASSERT_EQ(entries.size(), 3U);
        EXPECT_EQ(entries[0], (desktop::AutostartEntry{.id = "nm-applet.desktop", .name = "NetworkManager Applet", .command = "nm-applet",
                                                       .file = root / "user/nm-applet.desktop", .users = true, .on = false, .forDesktop = true}));
        EXPECT_EQ(entries[1].id, "baloo.desktop");
        EXPECT_FALSE(entries[1].forDesktop);
        EXPECT_FALSE(entries[2].on);
        EXPECT_TRUE(desktop::autostartEntries(root / "user", system, "KDE:Plasma")[1].forDesktop);
        std::filesystem::remove_all(root);
    }

    TEST(AutostartEntries, SwitchesAnEntryOnAndOff) {
        const std::string entry = "# A comment\n[Desktop Entry]\nName=Applet\nExec=applet\n\n[Desktop Action New]\nHidden=true\n";
        const auto        off   = desktop::withEntryOn(entry, false);
        EXPECT_EQ(off, "# A comment\n[Desktop Entry]\nHidden=true\nName=Applet\nExec=applet\n\n[Desktop Action New]\nHidden=true\n");
        EXPECT_EQ(desktop::withEntryOn(off, true), "# A comment\n[Desktop Entry]\nHidden=false\nName=Applet\nExec=applet\n\n[Desktop Action New]\nHidden=true\n");
        EXPECT_EQ(desktop::withEntryOn("[Desktop Entry]\nX-GNOME-Autostart-enabled=false\n", true), "[Desktop Entry]\nX-GNOME-Autostart-enabled=true\n");
        EXPECT_EQ(desktop::withEntryOn("", false), "[Desktop Entry]\nHidden=true\n");
    }

}
