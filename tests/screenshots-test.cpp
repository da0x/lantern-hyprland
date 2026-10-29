// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/lua-binds.hpp"
#include "desktop/popups.hpp"
#include "desktop/screenshots.hpp"

#include <gtest/gtest.h>

#include <format>
#include <string>

namespace {

    TEST(LuaBinds, ReadsAndWritesBindsOnALineOfTheirOwn) {
        const std::string config = "hl.bind(\"SUPER + V\", hl.dsp.layout(\"togglesplit\"))\n"
                                   "hl.bind(\"Print\", hl.dsp.exec_cmd([[grim -g \"$(slurp)\" ~/Pictures/screenshot-$(date +%F_%T).png]]))\n"
                                   "for i = 1, 9 do\n"
                                   "    hl.bind(\"SUPER + \" .. i, hl.dsp.focus({ workspace = i }))\n"
                                   "end\n";
        const auto binds = configuration::luaBindLines(config);
        ASSERT_EQ(binds.size(), 2U);
        EXPECT_EQ(binds[0].keys, "SUPER + V");
        EXPECT_EQ(binds[0].action, "hl.dsp.layout(\"togglesplit\")");
        EXPECT_FALSE(binds[0].command.has_value());
        EXPECT_EQ(binds[1].command, "grim -g \"$(slurp)\" ~/Pictures/screenshot-$(date +%F_%T).png");
        EXPECT_TRUE(configuration::sameKeys("super+v", "SUPER + V"));
        EXPECT_FALSE(configuration::sameKeys("SUPER + SHIFT + V", "SUPER + V"));

        const auto replaced = configuration::withLuaExecBind(config, "super + v", "kitty");
        EXPECT_TRUE(replaced.starts_with("hl.bind(\"super + v\", hl.dsp.exec_cmd(\"kitty\"))\nhl.bind(\"Print\""));
        const auto added = configuration::withLuaExecBind(config, "SUPER + SHIFT + V", "a \"b\"");
        EXPECT_TRUE(added.contains(".png]]))\nhl.bind(\"SUPER + SHIFT + V\", hl.dsp.exec_cmd([[a \"b\"]]))\nfor i"));
        EXPECT_EQ(configuration::withoutLuaBind(added, "SUPER + SHIFT + V"), config);
        EXPECT_EQ(configuration::withLuaExecBind("", "Print", "grim"), "\nhl.bind(\"Print\", hl.dsp.exec_cmd(\"grim\"))\n");
    }

    TEST(Screenshots, ReadsAndWritesTheKeys) {
        const std::string config = "hl.bind(\"Print\", hl.dsp.exec_cmd([[grim -g \"$(slurp)\" - | tee ~/Pictures/screenshot-$(date +%F_%T).png | wl-copy]]))\n";
        EXPECT_EQ(desktop::screenshotsIn(config), (desktop::Screenshots{.folder = "~/Pictures", .copy = true, .annotate = false}));
        EXPECT_FALSE(desktop::screenshotsIn("hl.bind(\"Print\", hl.dsp.exec_cmd(\"hyprshot -m region\"))\n").has_value());

        const desktop::Screenshots wanted{.folder = "~/Pictures/Screenshots", .copy = true, .annotate = true};
        const auto                 text = desktop::withScreenshots(config, wanted);
        EXPECT_EQ(desktop::screenshotsIn(text), wanted);
        EXPECT_EQ(configuration::luaBindLines(text).size(), 3U);
        EXPECT_EQ(desktop::screenshotCommand(wanted, desktop::Shot::screens),
                  "mkdir -p ~/Pictures/Screenshots && grim - | satty --filename - --output-filename ~/Pictures/Screenshots/$(date +%F_%H-%M-%S).png --early-exit "
                  "--copy-command wl-copy");
        EXPECT_EQ(desktop::screenshotCommand({.folder = "~/Pictures", .copy = false, .annotate = false}, desktop::Shot::area),
                  "mkdir -p ~/Pictures && grim -g \"$(slurp)\" ~/Pictures/$(date +%F_%H-%M-%S).png");
    }

    TEST(Screenshots, FindsTheClipboardHistoryKey) {
        const auto config = "hl.bind(\"SUPER + SHIFT + V\", hl.dsp.exec_cmd(\"" + desktop::clipboardPicker("wofi --dmenu") + "\"))\n";
        EXPECT_EQ(desktop::clipboardKeysIn(config), "SUPER + SHIFT + V");
        EXPECT_FALSE(desktop::clipboardKeysIn("").has_value());
    }

    TEST(LuaBinds, KeepsTheOptionsApartFromTheAction) {
        const auto binds = configuration::luaBindLines("hl.bind(\"XF86AudioMute\", hl.dsp.exec_cmd(\"wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle\"), { locked = true, repeating = true })\n");
        ASSERT_EQ(binds.size(), 1U);
        EXPECT_EQ(binds[0].command, "wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle");
        EXPECT_EQ(binds[0].options, "{ locked = true, repeating = true }");
    }

    TEST(Popups, BindsTheKeysToSwayosdAndBack) {
        const std::string config = "hl.on(\"hyprland.start\", function ()\n    hl.exec_cmd(\"waybar\")\nend)\n\n"
                                   "hl.bind(\"XF86AudioMute\", hl.dsp.exec_cmd(\"wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle\"), { locked = true })\n";
        const auto on = desktop::withPopups(config, true, false, false);
        EXPECT_TRUE(desktop::popupsIn(on));
        EXPECT_TRUE(on.contains("hl.bind(\"XF86AudioMute\", hl.dsp.exec_cmd(\"swayosd-client --output-volume mute-toggle\"), { locked = true })\n"));
        EXPECT_TRUE(on.contains("hl.bind(\"XF86AudioRaiseVolume\", hl.dsp.exec_cmd(\"swayosd-client --output-volume raise\"), { locked = true, repeating = true })\n"));
        EXPECT_FALSE(on.contains("brightness"));
        const auto off = desktop::withPopups(on, false, false, false);
        EXPECT_FALSE(desktop::popupsIn(off));
        EXPECT_FALSE(off.contains("swayosd"));
        EXPECT_TRUE(off.contains("hl.bind(\"XF86AudioMute\", hl.dsp.exec_cmd(\"wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle\"), { locked = true })\n"));
        EXPECT_TRUE(desktop::withPopups(config, true, true, false).contains("--brightness raise"));
    }

    TEST(Popups, BindsTheNumberPadAndUnbindsItAgain) {
        const std::string config = "hl.bind(\"SUPER + comma\", hl.dsp.exec_cmd(\"lantern-hyprland\"))\n";
        EXPECT_FALSE(desktop::numpadVolumeIn(desktop::withPopups(config, true, false, false)));
        const auto on = desktop::withPopups(config, true, false, true);
        EXPECT_TRUE(desktop::numpadVolumeIn(on));
        EXPECT_TRUE(on.contains("hl.bind(\"SUPER + KP_Add\", hl.dsp.exec_cmd(\"swayosd-client --output-volume raise\"), { locked = true, repeating = true })\n"));
        EXPECT_TRUE(on.contains("hl.bind(\"SUPER + KP_Subtract\", hl.dsp.exec_cmd(\"swayosd-client --output-volume lower\"), { locked = true, repeating = true })\n"));
        EXPECT_FALSE(desktop::withPopups(on, true, false, false).contains("KP_"));
        const auto off = desktop::withPopups(on, false, false, true);
        EXPECT_FALSE(off.contains("KP_"));
        EXPECT_TRUE(off.contains("SUPER + comma"));
    }

    TEST(Popups, WritesTheStyleInTheBarsColors) {
        const auto& palette = desktop::waybarPalettes().front();
        const auto  style   = desktop::popupStyle(palette);
        EXPECT_TRUE(desktop::lanternPopupStyle(style));
        EXPECT_TRUE(style.contains(std::format("background: {};", palette.accent)));
        EXPECT_FALSE(desktop::lanternPopupStyle("window#osd {}\n"));
    }

}
