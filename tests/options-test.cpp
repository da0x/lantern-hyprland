// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/options.hpp"
#include "ipc/options.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>

#include <unistd.h>

TEST(Options, ReadsHyprlandsDescriptions) {
    const auto described = ipc::parseOptionDescriptions(R"([
        {"name": "general:border_size", "description": "size of the border", "default": 1, "current": 1, "min": 0, "max": 20, "map": null},
        {"name": "general:gaps_in", "description": "gaps between windows", "default": "5 5 5 5", "current": "5 5 5 5", "min": null, "max": null},
        {"name": "decoration:dim_strength", "description": "how much", "default": 0.5, "current": 0},
        {"name": "input:follow_mouse", "description": "focus", "default": 1, "current": 1, "min": 0, "max": 3,
         "map": [{"separate": 3}, {"detached": 2}, {"follow": 1}, {"disabled": 0}]},
        {"name": "decoration:blur:enabled", "description": "blur", "default": true, "current": false}
    ])");
    ASSERT_TRUE(described) << described.error();
    ASSERT_EQ(described->size(), 5U);
    EXPECT_EQ((*described)[0].defaultValue, "1");
    EXPECT_EQ((*described)[0].maximum, 20);
    EXPECT_EQ((*described)[1].defaultValue, "5 5 5 5");
    EXPECT_FALSE((*described)[1].minimum);
    EXPECT_EQ((*described)[2].defaultValue, "0.5");
    ASSERT_EQ((*described)[3].choices.size(), 4U);
    EXPECT_EQ((*described)[3].choices.front(), (std::pair<std::string, long>{"disabled", 0}));
    EXPECT_EQ((*described)[4].defaultValue, "true");
    EXPECT_FALSE(ipc::parseOptionDescriptions("not json"));
}

TEST(Options, ReadsABatchOfValuesInOrderEvenPastAReplyThatIsNotJson) {
    const std::vector<std::string> names{"general:gaps_in", "misc:broken", "decoration:active_opacity", "decoration:shadow:offset", "input:kb_layout"};
    const auto                     values = ipc::parseOptionValues("{\"option\": \"general:gaps_in\", \"css\": \"5 5 5 5\", \"set\": true }\n\n\n"
                                                                   "invalid type (internal error)\n\n\n"
                                                                   "{\"option\": \"decoration:active_opacity\", \"float\": 1.000000, \"set\": false }\n\n\n"
                                                                   "{\"option\": \"decoration:shadow:offset\", \"vec2\": [0, 0], \"set\": false }\n\n\n"
                                                                   "{\"option\": \"input:kb_layout\", \"str\": \"us,ara\", \"set\": true }",
                                                                   names);
    ASSERT_EQ(values.size(), 5U);
    EXPECT_EQ(values[0], (ipc::OptionValue{"general:gaps_in", "5 5 5 5", true}));
    EXPECT_EQ(values[1], (ipc::OptionValue{"misc:broken", "", false}));
    EXPECT_EQ(values[2], (ipc::OptionValue{"decoration:active_opacity", "1", false}));
    EXPECT_EQ(values[3].value, "0 0");
    EXPECT_EQ(values[4], (ipc::OptionValue{"input:kb_layout", "us,ara", true}));
    EXPECT_EQ(ipc::numberText(0.2), "0.2");
}

TEST(Options, FindsTheCallThatSetEachOptionAndWhatDiffersFromTheDefault) {
    static std::atomic<int> counter = 0;
    const auto folder = std::filesystem::temp_directory_path() / ("lantern-hyprland-options-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
    std::filesystem::create_directories(folder);
    std::ofstream{folder / "hyprland.lua"} << "hl.config({\n"
                                              "    general = { gaps_in = 5, col = { active_border = { colors = { \"rgba(fabd2fee)\" }, angle = 45 } } },\n"
                                              "})\n"
                                              "hl.config({ decoration = { blur = { enabled = false } } })\n";
    const auto recording = configuration::record(folder / "hyprland.lua");
    std::filesystem::remove_all(folder);
    ASSERT_TRUE(recording) << recording.error();

    std::vector<ipc::OptionDescription> described;
    for (const auto* name : {"general:gaps_in", "general:col.active_border", "decoration:blur:enabled", "decoration:blur:size"})
        described.push_back(ipc::OptionDescription{.name = name, .description = {}, .defaultValue = "true", .minimum = {}, .maximum = {}, .choices = {}});
    described[0].defaultValue = "5 5 5 5";
    const auto options = application::joinOptions(described,
                                                   {{"general:gaps_in", "5 5 5 5", true},
                                                    {"general:col.active_border", "eefabd2f 45deg", true},
                                                    {"decoration:blur:enabled", "false", true},
                                                    {"decoration:blur:size", "8", false}},
                                                   recording);
    ASSERT_EQ(options.rows.size(), 4U);
    ASSERT_TRUE(options.rows[0].where);
    EXPECT_EQ(options.rows[0].where->line, 1);
    EXPECT_FALSE(options.rows[0].changed());
    ASSERT_TRUE(options.rows[1].where);
    EXPECT_TRUE(options.rows[1].changed());
    ASSERT_TRUE(options.rows[2].where);
    EXPECT_EQ(options.rows[2].where->line, 4);
    EXPECT_TRUE(options.rows[2].changed());
    EXPECT_FALSE(options.rows[3].where);
    EXPECT_FALSE(options.rows[3].changed());
}

#include "ipc/tree.hpp"

TEST(Tree, ReadsMonitorsWorkspacesAndWindows) {
    const auto tree = ipc::parseTree(
        R"([{"id": 0, "name": "DP-1", "description": "A screen", "width": 3840, "height": 2160, "refreshRate": 59.99, "x": 0, "y": 0, "scale": 2,
             "focused": true, "activeWorkspace": {"id": 2, "name": "2"}, "specialWorkspace": {"id": 0, "name": ""}}])",
        R"([{"id": -98, "name": "special:magic", "monitor": "DP-1", "windows": 0, "hasfullscreen": false},
            {"id": 2, "name": "2", "monitor": "DP-1", "windows": 1, "hasfullscreen": true},
            {"id": 1, "name": "1", "monitor": "DP-1", "windows": 0, "hasfullscreen": false}])",
        R"([{"address": "0x1", "class": "kitty", "title": "~", "workspace": {"id": 2, "name": "2"}, "monitor": 0, "at": [10, 20], "size": [800, 600],
             "floating": true, "fullscreen": 2, "pinned": false, "xwayland": false, "hidden": false, "pid": 42, "focusHistoryID": 0}])");
    ASSERT_TRUE(tree) << tree.error();
    ASSERT_EQ(tree->monitors.size(), 1U);
    EXPECT_EQ(tree->monitors[0].activeWorkspace, 2);
    EXPECT_EQ(tree->monitors[0].scale, 2);
    ASSERT_EQ(tree->workspaces.size(), 3U);
    EXPECT_EQ(tree->workspaces[0].id, 1);
    EXPECT_EQ(tree->workspaces[2].id, -98);
    ASSERT_EQ(tree->windows.size(), 1U);
    EXPECT_EQ(tree->windows[0].x, 10);
    EXPECT_EQ(tree->windows[0].height, 600);
    EXPECT_TRUE(tree->windows[0].floating);
    EXPECT_TRUE(tree->windows[0].fullscreen);
    EXPECT_FALSE(ipc::parseTree("[]", "{}", "[]"));
}

#include "ipc/request.hpp"

TEST(ConsoleCommand, SendsOnlyListedRequestsThatRead) {
    EXPECT_EQ(ipc::consoleCommand("monitors"), "j/monitors");
    EXPECT_EQ(ipc::consoleCommand("  hyprctl -j clients "), "j/clients");
    EXPECT_EQ(ipc::consoleCommand("j/version"), "j/version");
    EXPECT_EQ(ipc::consoleCommand("monitors all"), "j/monitors all");
    EXPECT_EQ(ipc::consoleCommand("getoption general:gaps_in"), "j/getoption general:gaps_in");

    EXPECT_FALSE(ipc::consoleCommand(""));
    EXPECT_FALSE(ipc::consoleCommand("dispatch exec kitty"));
    EXPECT_FALSE(ipc::consoleCommand("keyword general:gaps_in 0"));
    EXPECT_FALSE(ipc::consoleCommand("hyprctl reload"));
    EXPECT_FALSE(ipc::consoleCommand("clients;dispatch exec kitty"));
    EXPECT_FALSE(ipc::consoleCommand("[[BATCH]]j/clients"));
    EXPECT_FALSE(ipc::consoleCommand("getoption general:gaps_in;dispatch exec kitty"));
    EXPECT_FALSE(ipc::consoleCommand("getoption"));
    EXPECT_FALSE(ipc::consoleCommand("clients extra"));
    EXPECT_NE(ipc::consoleCommand("dispatch exec kitty").error().find("changes Hyprland"), std::string::npos);
}

TEST(ConsoleCommand, IndentsJsonInHyprlandsOrderAndTellsOtherRepliesApart) {
    EXPECT_EQ(ipc::indentedJson(R"({"y":1,"x":2})"), "{\n  \"y\": 1,\n  \"x\": 2\n}");
    EXPECT_EQ(ipc::indentedJson("\"splash\" - hyprBot"), std::nullopt);
}
