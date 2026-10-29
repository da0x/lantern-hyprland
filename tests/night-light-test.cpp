// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/autostart.hpp"
#include "desktop/night-light.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

    TEST(NightLight, ReadsAndWritesTheEveningAndMorning) {
        const std::string config = "max-gamma = 150\n\nprofile {\n    time = 7:30\n    identity = true\n}\n\nprofile {\n    time = 21:00\n    temperature = 5500\n    gamma = 0.8\n}\n";
        const auto        read   = desktop::nightLightIn(config);
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(*read, (desktop::NightLight{.temperature = 5500, .evening = "21:00", .morning = "07:30"}));

        const desktop::NightLight wanted{.temperature = 3500, .evening = "20:00", .morning = "06:00"};
        const auto                text = desktop::withNightLight(config, wanted);
        EXPECT_EQ(text, "max-gamma = 150\n\nprofile {\n    time = 06:00\n    identity = true\n}\n\nprofile {\n    time = 20:00\n    temperature = 3500\n}\n");
        EXPECT_EQ(desktop::nightLightIn(text), wanted);
        EXPECT_FALSE(desktop::nightLightIn("max-gamma = 150\n").has_value());
        EXPECT_EQ(desktop::withNightLight("", wanted), text.substr(std::string{"max-gamma = 150\n\n"}.size()));
    }

    TEST(NightLight, StopsStartingAtLogin) {
        const std::string config = "hl.on(\"hyprland.start\", function ()\n    hl.exec_cmd(\"waybar\")\n    hl.exec_cmd(\"hyprsunset\")\nend)\n";
        EXPECT_EQ(configuration::withoutAutostart(config, true, "hyprsunset"), "hl.on(\"hyprland.start\", function ()\n    hl.exec_cmd(\"waybar\")\nend)\n");
        EXPECT_EQ(configuration::withoutAutostart("exec-once = hyprsunset\nexec-once=waybar\n", false, "hyprsunset"), "exec-once=waybar\n");
    }

}
