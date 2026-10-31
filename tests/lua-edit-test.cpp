// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/hyprlang-edit.hpp"
#include "configuration/lua-edit.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

    constexpr auto config = R"x(-- my config
local accent = "rgba(ff0000ff)"

hl.config({
    general = {
        gaps_in  = 5,

        col = {
            active_border   = { colors = {"rgba(33ccffee)", "rgba(00ff99ee)"}, angle = 45 },
            inactive_border = "rgba(595959aa)", -- gray
        },
    },
})

hl.config({ decoration = { rounding = 10 } })
)x";

    TEST(LuaEdit, ReplacesOnlyTheValue) {
        const auto edited = configuration::withLuaConfigValue(config, {"general", "col", "inactive_border"}, "\"rgba(45475aaa)\"");
        ASSERT_TRUE(edited.has_value()) << edited.error();
        std::string expected = config;
        expected.replace(expected.find("\"rgba(595959aa)\""), 16, "\"rgba(45475aaa)\"");
        EXPECT_EQ(*edited, expected);
    }

    TEST(LuaEdit, ReplacesATableValueWhole) {
        const auto edited = configuration::withLuaConfigValue(config, {"general", "col", "active_border"}, R"x({ colors = {"rgba(cba6f7ee)", "rgba(89b4faee)"}, angle = 45 })x");
        ASSERT_TRUE(edited.has_value());
        EXPECT_TRUE(edited->contains(R"x(active_border   = { colors = {"rgba(cba6f7ee)", "rgba(89b4faee)"}, angle = 45 },)x"));
        EXPECT_TRUE(edited->contains("local accent = \"rgba(ff0000ff)\""));
    }

    TEST(LuaEdit, FindsAValueInALaterCall) {
        const auto edited = configuration::withLuaConfigValue(config, {"decoration", "rounding"}, "12");
        ASSERT_TRUE(edited.has_value());
        EXPECT_TRUE(edited->contains("hl.config({ decoration = { rounding = 12 } })"));
    }

    TEST(LuaEdit, RefusesWhatTheConfigDoesNotWriteInPlace) {
        EXPECT_FALSE(configuration::withLuaConfigValue(config, {"general", "col", "nogroup_border"}, "\"x\"").has_value());
        EXPECT_FALSE(configuration::withLuaConfigValue("local c = {}\nhl.config(c)\n", {"general"}, "{}").has_value());
    }

    TEST(LuaEdit, AddsACallForAValueNotWrittenInPlace) {
        const auto edited = configuration::withLuaConfigValueSet(config, {"decoration", "screen_shader"}, "\"/s.frag\"");
        EXPECT_EQ(edited, std::string{config} + "\nhl.config({ decoration = { screen_shader = \"/s.frag\" } })\n");
        // Then found in place, and changed there.
        const auto again = configuration::withLuaConfigValueSet(edited, {"decoration", "screen_shader"}, "\"\"");
        EXPECT_EQ(again, std::string{config} + "\nhl.config({ decoration = { screen_shader = \"\" } })\n");
        EXPECT_EQ(configuration::withLuaConfigValueSet(config, {"decoration", "rounding"}, "4"), *configuration::withLuaConfigValue(config, {"decoration", "rounding"}, "4"));
    }

    TEST(LuaEdit, SetsAnEnvironmentVariable) {
        const auto config = "-- hl.env(\"XCURSOR_SIZE\", \"16\")\nhl.env(\"XCURSOR_SIZE\", \"24\")\nhl.env(\"OTHER\", \"1\")\n";
        EXPECT_EQ(configuration::luaEnv(config, "XCURSOR_SIZE"), std::optional<std::string>{"24"});
        EXPECT_EQ(configuration::withLuaEnv(config, "XCURSOR_SIZE", "48"), "-- hl.env(\"XCURSOR_SIZE\", \"16\")\nhl.env(\"XCURSOR_SIZE\", \"48\")\nhl.env(\"OTHER\", \"1\")\n");
        EXPECT_EQ(configuration::withLuaEnv("hl.config({})", "HYPRCURSOR_SIZE", "32"), "hl.config({})\nhl.env(\"HYPRCURSOR_SIZE\", \"32\")\n");
        EXPECT_FALSE(configuration::luaEnv(config, "MISSING").has_value());
    }

    TEST(HyprlangEdit, SetsKeysOutsideBlocksAndAddsMissingOnes) {
        const auto config = "# colors\naccent = 0xff00ffcc   # mine\nwallpaper {\n    accent = 1\n}\ntext=0xffdadada\n";
        const auto edited = configuration::withHyprlangValues(config, {{"accent", "0xffcba6f7"}, {"text", "0xffcdd6f4"}, {"base", "0xff313244"}});
        EXPECT_EQ(edited, "# colors\naccent = 0xffcba6f7   # mine\nwallpaper {\n    accent = 1\n}\ntext=0xffcdd6f4\nbase = 0xff313244\n");
        EXPECT_EQ(configuration::withHyprlangValues("", {{"accent", "0x1"}}), "accent = 0x1\n");
    }

    TEST(IniEdit, SetsKeysInTheirSectionsLosslessly) {
        const auto config = "[global]\n    font = Monospace 8\n    frame_color = \"#aaaaaa\"\n\n[urgency_low]\n    background = \"#222222\"\n    # foreground = \"#888888\"\n\n";
        const auto edited = configuration::withIniValues(config, {{"global", "frame_color", "\"#cba6f7\""},
                                                                  {"urgency_low", "background", "\"#1e1e2e\""},
                                                                  {"urgency_low", "foreground", "\"#cdd6f4\""},
                                                                  {"urgency_low", "frame_color", "\"#313244\""},
                                                                  {"urgency_critical", "background", "\"#f38ba8\""}});
        EXPECT_EQ(edited, "[global]\n    font = Monospace 8\n    frame_color = \"#cba6f7\"\n\n[urgency_low]\n    background = \"#1e1e2e\"\n    foreground = \"#cdd6f4\"\n"
                          "    frame_color = \"#313244\"\n\n\n[urgency_critical]\n    background = \"#f38ba8\"\n");
    }

    TEST(SpacedEdit, SetsTheLastLineOrAddsOne) {
        const auto config = "font_family JetBrains Mono\n  font_size 10\n# font_size 20\nfont_size 12.5\n";
        EXPECT_EQ(configuration::spacedValue(config, "font_size"), std::optional<std::string>{"12.5"});
        EXPECT_EQ(configuration::withSpacedValue(config, "font_size", "14"), "font_family JetBrains Mono\n  font_size 10\n# font_size 20\nfont_size 14\n");
        EXPECT_EQ(configuration::withSpacedValue("include a.conf", "font_size", "14"), "include a.conf\nfont_size 14\n");
        EXPECT_FALSE(configuration::spacedValue("# font_size 9\n", "font_size").has_value());
    }

    TEST(LuaEdit, ReplacesAndRemovesANamedBlock) {
        const std::string mine = "-- mine\nhl.config({})\n";
        const auto        added = configuration::withLuaBlock(mine, "a test", "local a = 1\n", true);
        EXPECT_EQ(added, "-- mine\nhl.config({})\n\n-- Lantern: a test\nlocal a = 1\n-- Lantern: end of a test\n");
        EXPECT_EQ(configuration::luaBlock(added, "a test"), "local a = 1\n");
        const auto changed = configuration::withLuaBlock(added + "-- after\n", "a test", "local a = 2", true);
        EXPECT_EQ(changed, "-- mine\nhl.config({})\n\n-- Lantern: a test\nlocal a = 2\n-- Lantern: end of a test\n-- after\n");
        EXPECT_EQ(configuration::withLuaBlock(added, "a test", "", false), mine);
        EXPECT_FALSE(configuration::luaBlock(mine, "a test").has_value());
        EXPECT_EQ(configuration::withLuaBlock(mine, "a test", "", false), mine);
    }

}
