// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/default-apps.hpp"

#include <gtest/gtest.h>

#include <array>
#include <fstream>
#include <unistd.h>

namespace {

    void write(const std::filesystem::path& file, std::string_view text) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream{file} << text;
    }

    TEST(DefaultApps, FindsTheApplicationsAndTheirDefaults) {
        const auto root = std::filesystem::temp_directory_path() / ("lantern-hyprland-default-apps-" + std::to_string(::getpid()));
        std::filesystem::remove_all(root);
        write(root / "user/firefox.desktop", "[Desktop Entry]\nType=Application\nName=Firefox Nightly\nMimeType=text/html;x-scheme-handler/https;\n");
        write(root / "system/firefox.desktop", "[Desktop Entry]\nName=Firefox\n");
        write(root / "system/kitty.desktop", "[Desktop Entry]\nName=kitty\nCategories=System;TerminalEmulator;\n");
        write(root / "system/gone.desktop", "[Desktop Entry]\nName=Gone\nHidden=true\n");
        write(root / "system/kde/okular.desktop", "[Desktop Entry]\nName=Okular\nMimeType=application/pdf;\n\n[Desktop Action New]\nName=New\n");

        const std::array folders{root / "user", root / "system"};
        const auto       installed = desktop::applications(folders);
        ASSERT_EQ(installed.size(), 3U);
        EXPECT_EQ(installed[0], (desktop::Application{.id = "firefox.desktop", .name = "Firefox Nightly", .types = {"text/html", "x-scheme-handler/https"}, .terminal = false}));
        EXPECT_EQ(installed[1].id, "kde-okular.desktop");
        EXPECT_TRUE(installed[2].terminal);

        const std::array<std::string, 2> lists{"[Default Applications]\napplication/pdf=missing.desktop;kde-okular.desktop;\n",
                                               "[Default Applications]\napplication/pdf=firefox.desktop\ntext/html=firefox.desktop\n"};
        EXPECT_EQ(desktop::defaultFor(lists, "application/pdf", installed), "kde-okular.desktop");
        EXPECT_EQ(desktop::defaultFor(lists, "text/html", installed), "firefox.desktop");
        EXPECT_FALSE(desktop::defaultFor(lists, "image/png", installed).has_value());
        const std::array<std::string, 1> caches{"[MIME Cache]\napplication/pdf=kde-okular.desktop;\ntext/html=missing.desktop;firefox.desktop;\n"};
        const std::array<std::string, 1> added{"[Added Associations]\napplication/pdf=firefox.desktop;\n"};
        EXPECT_EQ(desktop::fallbackFor({}, caches, "text/html", installed), "firefox.desktop");
        EXPECT_EQ(desktop::fallbackFor(added, caches, "application/pdf", installed), "firefox.desktop");
        EXPECT_FALSE(desktop::fallbackFor(added, caches, "image/png", installed).has_value());
        std::filesystem::remove_all(root);
    }

    TEST(DefaultApps, WritesADefaultForEachType) {
        const std::array<std::string, 2> types{"text/html", "x-scheme-handler/https"};
        EXPECT_EQ(desktop::withDefault("[Default Applications]\nx-scheme-handler/irc=hexchat.desktop\ntext/html=chromium.desktop;firefox.desktop;\n\n[Added Associations]\nimage/png=a.desktop;\n",
                                       types, "firefox.desktop"),
                  "[Default Applications]\nx-scheme-handler/irc=hexchat.desktop\ntext/html=firefox.desktop;chromium.desktop;\nx-scheme-handler/https=firefox.desktop;\n\n"
                  "[Added Associations]\nimage/png=a.desktop;\n");
        EXPECT_EQ(desktop::withDefault("", types, "firefox.desktop"), "[Default Applications]\ntext/html=firefox.desktop;\nx-scheme-handler/https=firefox.desktop;\n");
        EXPECT_EQ(desktop::withDefault("[Added Associations]\n", types, "a.desktop"),
                  "[Added Associations]\n\n[Default Applications]\ntext/html=a.desktop;\nx-scheme-handler/https=a.desktop;\n");
    }

    TEST(DefaultApps, PutsTheTerminalFirst) {
        EXPECT_EQ(desktop::preferredTerminal("# mine\nAlacritty.desktop\nkitty.desktop\n"), "Alacritty.desktop");
        EXPECT_EQ(desktop::withTerminal("Alacritty.desktop\nkitty.desktop\n", "kitty.desktop"), "kitty.desktop\nAlacritty.desktop\n");
        EXPECT_FALSE(desktop::preferredTerminal("").has_value());
    }

}
