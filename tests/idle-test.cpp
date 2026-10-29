// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/idle.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace {

    TEST(Idle, WritesHypridleFromNothing) {
        const desktop::IdleSettings settings{.dim = 0, .screensaver = 0, .lock = 5, .screensOff = 6, .suspend = 0, .lockBeforeSleep = true};
        const auto                  text = desktop::withIdle("", settings);
        EXPECT_EQ(text, "general {\n    lock_cmd = pidof hyprlock || hyprlock\n    before_sleep_cmd = loginctl lock-session\n"
                        "    after_sleep_cmd = hyprctl dispatch 'hl.dsp.dpms({ action = \"on\" })'\n}\n\n"
                        "listener {\n    timeout = 300\n    on-timeout = loginctl lock-session\n}\n\n"
                        "listener {\n    timeout = 360\n    on-timeout = hyprctl dispatch 'hl.dsp.dpms({ action = \"off\" })'\n"
                        "    on-resume = hyprctl dispatch 'hl.dsp.dpms({ action = \"on\" })'\n}\n");
        EXPECT_EQ(desktop::idleIn(text), settings);
    }

    TEST(Idle, KeepsWhatItDoesNotKnow) {
        const std::string config = "# mine\ngeneral {\n    lock_cmd = hyprlock\n    before_sleep_cmd = loginctl lock-session  # lock first\n}\n\n"
                                   "listener {\n    timeout = 150\n    on-timeout = notify-send away\n}\n\n"
                                   "listener {\n    timeout = 600\n    on-timeout = loginctl lock-session\n}\n";
        const auto read = desktop::idleIn(config);
        EXPECT_EQ(read.lock, 10);
        EXPECT_TRUE(read.lockBeforeSleep);

        const desktop::IdleSettings wanted{.dim = 0, .screensaver = 0, .lock = 15, .screensOff = 0, .suspend = 30, .lockBeforeSleep = false};
        const auto                  text = desktop::withIdle(config, wanted);
        EXPECT_TRUE(text.starts_with("# mine\ngeneral {\n    lock_cmd = hyprlock\n    after_sleep_cmd = hyprctl dispatch")) << text;
        EXPECT_FALSE(text.contains("before_sleep_cmd")) << text;
        EXPECT_TRUE(text.contains("listener {\n    timeout = 150\n    on-timeout = notify-send away\n}\n\nlistener {\n    timeout = 900\n")) << text;
        EXPECT_TRUE(text.ends_with("listener {\n    timeout = 1800\n    on-timeout = systemctl suspend\n}\n")) << text;
        EXPECT_FALSE(text.contains("\n\n\n")) << text;
        EXPECT_EQ(desktop::idleIn(text), wanted);
    }

    TEST(Idle, DrawsTheLockScreenInThePalette) {
        const auto& palette = desktop::waybarPalettes().front();
        const auto  text    = desktop::lockScreen(palette);
        EXPECT_TRUE(desktop::lanternLockScreen(text));
        EXPECT_FALSE(desktop::lanternLockScreen("general {\n}\n"));
        EXPECT_TRUE(text.contains(std::format("outer_color = rgb({})", palette.accent.substr(1)))) << text;
    }

    TEST(Idle, StartsTheScreensaverWithGraceUntilTheLock) {
        const desktop::IdleSettings settings{.dim = 0, .screensaver = 5, .lock = 15, .screensOff = 0, .suspend = 0, .lockBeforeSleep = false};
        const auto                  text = desktop::withIdle("", settings);
        EXPECT_TRUE(text.contains("listener {\n    timeout = 300\n    on-timeout = pidof hyprlock || hyprlock --grace 600\n}\n\nlistener {\n    timeout = 900\n")) << text;
        EXPECT_EQ(desktop::idleIn(text), settings);
        // With no lock after it, the screensaver never asks for the password.
        const auto never = desktop::withIdle("", desktop::IdleSettings{.dim = 0, .screensaver = 10, .lock = 0, .screensOff = 0, .suspend = 0, .lockBeforeSleep = false});
        EXPECT_TRUE(never.contains("hyprlock --grace 86400")) << never;
        EXPECT_EQ(desktop::idleIn(never).screensaver, 10);
        EXPECT_EQ(desktop::idleIn(never).lock, 0);
    }

    TEST(Idle, DrawsTheLockScreenAsDesigned) {
        const auto&               palette = desktop::waybarPalettes().front();
        const desktop::LockDesign design{.background = desktop::LockBackground::wallpaper, .clock = desktop::LockClock::small, .date = false};
        const auto                text = desktop::lockScreen(palette, design, std::filesystem::path{"/home/me/Pictures/Wallpapers/hill.jpg"});
        EXPECT_EQ(desktop::lockDesignIn(text), design);
        EXPECT_TRUE(text.contains("    path = /home/me/Pictures/Wallpapers/hill.jpg\n")) << text;
        EXPECT_TRUE(text.contains("font_size = 48")) << text;
        EXPECT_FALSE(text.contains("date +")) << text;

        const auto plain = desktop::lockScreen(palette, {.background = desktop::LockBackground::color, .clock = desktop::LockClock::none, .date = true});
        EXPECT_FALSE(plain.contains("path =")) << plain;
        EXPECT_FALSE(plain.contains("$TIME")) << plain;
        EXPECT_TRUE(plain.contains("text = cmd[update:60000] date +\"%A, %B %-d\"")) << plain;
        EXPECT_EQ(desktop::lockDesignIn(desktop::lockScreen(palette)), desktop::LockDesign{});
        EXPECT_FALSE(desktop::lockDesignIn("general {\n}\n").has_value());
    }

    TEST(Idle, BindsOneKeyToLockAtOnce) {
        const std::string config = "hl.bind(\"SUPER + L\", hl.dsp.focus({ direction = \"right\" }))\n";
        EXPECT_EQ(desktop::lockKeyIn(config), "");
        const auto escape = desktop::withLockKey(config, "SUPER + Escape");
        EXPECT_EQ(desktop::lockKeyIn(escape), "SUPER + Escape");
        EXPECT_TRUE(escape.contains("SUPER + L")) << escape;
        const auto moved = desktop::withLockKey(escape, "CTRL + ALT + L");
        EXPECT_EQ(desktop::lockKeyIn(moved), "CTRL + ALT + L");
        EXPECT_FALSE(moved.contains("SUPER + Escape")) << moved;
        EXPECT_EQ(desktop::withLockKey(moved, ""), config);
    }

}
