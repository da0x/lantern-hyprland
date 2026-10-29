// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/essentials.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

namespace {

    std::filesystem::path made(const std::filesystem::path& file, const std::string& text = "") {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream{file} << text;
        return file;
    }

    TEST(Essentials, FindsWhatIsThereAndWhatIsNot) {
        const auto root = std::filesystem::temp_directory_path() / ("lantern-hyprland-essentials-" + std::to_string(::getpid()));
        std::filesystem::remove_all(root);
        // No processes run in this folder, so every agent found is only installed.
        made(root / "proc" / "none");
        made(root / "usr/share/xdg-desktop-portal/portals/hyprland.portal", "[portal]\nInterfaces=org.freedesktop.impl.portal.Screenshot;org.freedesktop.impl.portal.FileChooser;\n");
        made(root / "usr/lib/polkit-kde-authentication-agent-1");
        made(root / "usr/share/dbus-1/services/org.knopwob.dunst.service", "[D-BUS Service]\nName=org.freedesktop.Notifications\n");
        const desktop::EssentialSources sources{.root = root, .processes = root / "proc", .hasFont = [](const std::string& family) { return family == "IBM Plex Sans"; }};

        const auto found = desktop::essentials(sources);
        ASSERT_EQ(found.size(), 7U);
        const auto named = [&found](std::string_view name) {
            for (const auto& essential : found)
                if (essential.requirement.name == name)
                    return essential;
            ADD_FAILURE() << name;
            return desktop::Essential{};
        };
        EXPECT_TRUE(named("Screen sharing").requirement.met());
        // Hyprland's portal says it picks files, but has no dialog for it.
        EXPECT_FALSE(named("Open and save dialogs").requirement.met());
        const auto agent = named("Password prompts");
        EXPECT_TRUE(agent.requirement.met());
        EXPECT_FALSE(agent.problem.empty());
        EXPECT_EQ(agent.start, std::filesystem::path{"/usr/lib/polkit-kde-authentication-agent-1"});
        EXPECT_EQ(named("Notifications").requirement.found, "org.knopwob.dunst, started when needed");
        EXPECT_EQ(named("A font for text").requirement.found, "IBM Plex Sans");
        EXPECT_FALSE(named("Emoji").requirement.met());
        EXPECT_FALSE(named("Qt apps on Wayland").requirement.met());

        made(root / "usr/share/xdg-desktop-portal/portals/gtk.portal", "[portal]\nInterfaces=org.freedesktop.impl.portal.FileChooser;\n");
        EXPECT_EQ(desktop::essentials(sources)[1].requirement.found, "the gtk portal");
        std::filesystem::remove_all(root);
    }

}
