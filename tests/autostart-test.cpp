// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/autostart.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>
#include <string_view>

namespace {

    constexpr std::array<std::string_view, 1> hyprpaper{"hyprpaper"};
    constexpr std::array<std::string_view, 1> awww{"awww-daemon"};

    TEST(Autostart, AddsTheLineAtTheEndOfTheStartBlock) {
        const auto config = "-- autostart\n"
                            "hl.on(\"hyprland.start\", function ()\n"
                            "    hl.exec_cmd(\"waybar\")\n"
                            "end)\n"
                            "\n"
                            "hl.env(\"A\", \"b\")\n";
        EXPECT_EQ(configuration::withAutostart(config, true, "awww-daemon", hyprpaper), "-- autostart\n"
                                                                                       "hl.on(\"hyprland.start\", function ()\n"
                                                                                       "    hl.exec_cmd(\"waybar\")\n"
                                                                                       "    hl.exec_cmd(\"awww-daemon\")\n"
                                                                                       "end)\n"
                                                                                       "\n"
                                                                                       "hl.env(\"A\", \"b\")\n");
        EXPECT_FALSE(configuration::startsAtLogin(config, true, "awww-daemon"));
    }

    TEST(Autostart, SwapsOneProgramForTheOther) {
        const auto config = "hl.on(\"hyprland.start\", function ()\n"
                            "\thl.exec_cmd(\"waybar\")\n"
                            "\thl.exec_cmd(\"awww-daemon\")\n"
                            "end)\n";
        const auto swapped = configuration::withAutostart(config, true, "hyprpaper", awww);
        EXPECT_EQ(swapped, "hl.on(\"hyprland.start\", function ()\n"
                           "\thl.exec_cmd(\"waybar\")\n"
                           "\thl.exec_cmd(\"hyprpaper\")\n"
                           "end)\n");
        EXPECT_TRUE(configuration::startsAtLogin(swapped, true, "hyprpaper"));
        EXPECT_FALSE(configuration::startsAtLogin(swapped, true, "awww-daemon"));
    }

    TEST(Autostart, LeavesAConfigThatAlreadyStartsItAsItIs) {
        const auto config = "hl.on(\"hyprland.start\", function ()\n    hl.exec_cmd(\"hyprpaper\")\nend)\n";
        EXPECT_EQ(configuration::withAutostart(config, true, "hyprpaper", awww), config);
    }

    TEST(Autostart, AddsABlockWhenThereIsNone) {
        EXPECT_EQ(configuration::withAutostart("hl.env(\"A\", \"b\")", true, "hyprpaper", awww),
                  "hl.env(\"A\", \"b\")\n\nhl.on(\"hyprland.start\", function ()\n    hl.exec_cmd(\"hyprpaper\")\nend)\n");
    }

    TEST(Autostart, LeavesOtherWaysOfStartingAProgramAlone) {
        const auto config = "hl.on(\"hyprland.start\", function ()\n    hl.exec_cmd(\"awww-daemon --quiet\")\nend)\n";
        const auto edited = configuration::withAutostart(config, true, "hyprpaper", awww);
        EXPECT_TRUE(edited.contains("awww-daemon --quiet"));
    }

    TEST(Autostart, WritesExecOnceInHyprlandConf) {
        const auto config = "exec-once = waybar\nexec-once=hyprpaper\n$mod = SUPER\n";
        EXPECT_EQ(configuration::withAutostart(config, false, "awww-daemon", hyprpaper), "exec-once = waybar\n$mod = SUPER\n\nexec-once = awww-daemon\n");
        EXPECT_TRUE(configuration::startsAtLogin(config, false, "hyprpaper"));
    }

    TEST(Autostart, ListsAndSwitchesWhatStartsAtLogin) {
        const auto config = "hl.exec_cmd(\"not-at-login\")\n"
                            "hl.on(\"hyprland.start\", function ()\n"
                            "    hl.exec_cmd(\"waybar\")\n"
                            "    -- hl.exec_cmd(\"nm-applet --indicator\")\n"
                            "    hl.exec_cmd(\"sh -c \\\"echo hi\\\"\")\n"
                            "end)\n";
        EXPECT_EQ(configuration::startedAtLogin(config, true), (std::vector<configuration::StartedProgram>{
                                                                   {"waybar", true}, {"nm-applet --indicator", false}, {"sh -c \"echo hi\"", true}}));
        const auto off = configuration::withAutostartSwitched(config, true, "waybar", false);
        EXPECT_NE(off.find("    -- hl.exec_cmd(\"waybar\")\n"), std::string::npos);
        EXPECT_EQ(configuration::withAutostartSwitched(off, true, "waybar", true), config);
        EXPECT_NE(configuration::withAutostartSwitched(config, true, "nm-applet --indicator", true).find("    hl.exec_cmd(\"nm-applet --indicator\")\n"), std::string::npos);
        EXPECT_EQ(configuration::withoutAutostart(config, true, "nm-applet --indicator").find("nm-applet"), std::string::npos);
        EXPECT_NE(configuration::withAutostart("", true, "sh -c \"echo hi\"", {}).find("hl.exec_cmd(\"sh -c \\\"echo hi\\\"\")"), std::string::npos);

        const auto conf = "exec-once = waybar\n# exec-once = nm-applet\nexec = not-once\n";
        EXPECT_EQ(configuration::startedAtLogin(conf, false), (std::vector<configuration::StartedProgram>{{"waybar", true}, {"nm-applet", false}}));
        EXPECT_EQ(configuration::withAutostartSwitched(conf, false, "waybar", false), "# exec-once = waybar\n# exec-once = nm-applet\nexec = not-once\n");
        EXPECT_EQ(configuration::withAutostartSwitched(conf, false, "nm-applet", true), "exec-once = waybar\nexec-once = nm-applet\nexec = not-once\n");
    }
}
