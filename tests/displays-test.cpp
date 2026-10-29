// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/lua-edit.hpp"
#include "desktop/displays.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

namespace {

    constexpr std::string_view reply = R"([{
        "id": 0, "name": "DP-10", "description": "Samsung Electric Company Example 27 0000001",
        "width": 3840, "height": 2160, "refreshRate": 30.00000, "x": 0, "y": 0, "scale": 1, "transform": 0,
        "vrr": false, "disabled": false,
        "availableModes": ["3840x2160@30.00Hz", "3840x2160@29.97Hz", "1920x1080@120.00Hz", "1920x1080@120.00Hz", "1920x1080@60.00Hz"]
    }, {
        "id": 1, "name": "eDP-1", "description": "BOE 0x0BCA", "width": 1920, "height": 1200, "refreshRate": 59.95,
        "x": 3840, "y": 0, "scale": 1.25, "transform": 1, "vrr": true, "disabled": false, "availableModes": ["1920x1200@59.95Hz"]
    }])";

    const std::string config = "-- Monitors\n"
                               "hl.monitor({\n"
                               "    output   = \"\",\n"
                               "    mode     = \"preferred\",\n"
                               "    position = \"auto\",\n"
                               "    scale    = \"auto\",\n"
                               "})\n"
                               "\n"
                               "hl.monitor({ output = \"eDP-1\", mode = \"preferred\" })\n"
                               "\n"
                               "hl.bind(\"SUPER + Q\", hl.dsp.exec_cmd(\"kitty\"))\n";

    TEST(Displays, ReadsEachModeOnce) {
        const auto displays = desktop::parseDisplays(reply);
        ASSERT_TRUE(displays.has_value()) << displays.error();
        ASSERT_EQ(displays->size(), 2U);
        const auto& samsung = (*displays)[0];
        EXPECT_EQ(samsung.settings.mode, (desktop::Mode{3840, 2160, 30}));
        EXPECT_EQ(samsung.modes.size(), 4U);
        EXPECT_EQ(desktop::modeText(samsung.modes[1]), "3840x2160@29.97");
        EXPECT_EQ(desktop::modeText(samsung.modes[2]), "1920x1080@120");
        const auto& laptop = (*displays)[1];
        EXPECT_EQ(laptop.settings.transform, 1);
        EXPECT_EQ(laptop.settings.vrr, 1);
        // Turned on its side, at 1.25: 1200 by 1920 divided by 1.25.
        EXPECT_EQ(desktop::layoutSize(laptop.settings), (std::pair{960, 1536}));
    }

    TEST(Displays, OffersScalesThatMakeWholePixels) {
        EXPECT_EQ(desktop::scalesFor({3840, 2160, 60}, 1), (std::vector<double>{1, 1.25, 1.5, 1.6, 2, 2.5, 3}));
        EXPECT_EQ(desktop::scalesFor({2560, 1600, 60}, 1), (std::vector<double>{1, 1.25, 1.6, 2, 2.5}));
        EXPECT_EQ(desktop::scalesFor({1920, 1080, 60}, 1.33), (std::vector<double>{1, 1.25, 1.33, 1.5, 1.6, 2, 2.5, 3}));
    }

    TEST(Displays, PlacesOneBesideAnother) {
        const desktop::DisplaySettings big{.mode = {3840, 2160, 60}, .x = 0, .y = 0, .scale = 2};
        const desktop::DisplaySettings small{.mode = {1920, 1080, 60}};
        EXPECT_EQ(desktop::positionBeside(small, big, desktop::Beside::right), (std::pair{1920, 0}));
        EXPECT_EQ(desktop::positionBeside(small, big, desktop::Beside::left), (std::pair{-1920, 0}));
        EXPECT_EQ(desktop::positionBeside(small, big, desktop::Beside::below), (std::pair{0, 1080}));
    }

    TEST(Displays, AddsACallOfItsOwnAfterTheCatchAll) {
        const auto edited = configuration::withLuaMonitor(
            config, [](std::string_view output) { return output == "DP-10"; }, "desc:Samsung", {{"mode", "\"3840x2160@60\""}, {"position", "\"0x0\""}, {"scale", "1"}});
        ASSERT_TRUE(edited.has_value()) << edited.error();
        EXPECT_TRUE(edited->contains("hl.monitor({ output = \"eDP-1\", mode = \"preferred\" })\n\n"
                                     "hl.monitor({\n"
                                     "    output   = \"desc:Samsung\",\n"
                                     "    mode     = \"3840x2160@60\",\n"
                                     "    position = \"0x0\",\n"
                                     "    scale    = 1,\n"
                                     "})\n\nhl.bind"))
            << *edited;
        EXPECT_EQ(configuration::luaMonitorOutputs(*edited), (std::vector<std::string>{"", "eDP-1", "desc:Samsung"}));
    }

    TEST(Displays, ChangesACallInPlaceAndAddsWhatItLacks) {
        const auto catchAll = [](std::string_view output) { return output.empty(); };
        const auto edited   = configuration::withLuaMonitor(config, catchAll, "", {{"mode", "\"1920x1080@120\""}, {"vrr", "1"}});
        ASSERT_TRUE(edited.has_value()) << edited.error();
        EXPECT_TRUE(edited->contains("    mode     = \"1920x1080@120\",\n    position = \"auto\",\n    scale    = \"auto\",\n    vrr      = 1,\n})\n")) << *edited;

        // On one line, the field goes on that line.
        const auto laptop = configuration::withLuaMonitor(config, [](std::string_view output) { return output == "eDP-1"; }, "eDP-1", {{"scale", "1.5"}});
        ASSERT_TRUE(laptop.has_value());
        EXPECT_TRUE(laptop->contains("hl.monitor({ output = \"eDP-1\", mode = \"preferred\", scale = 1.5 })\n")) << *laptop;
    }

    TEST(Displays, LeavesComputedValuesAlone) {
        const auto edited = configuration::withLuaMonitor("local s = 2\nhl.monitor({ output = \"DP-1\", scale = s })\n", [](std::string_view output) { return output == "DP-1"; }, "DP-1",
                                                          {{"scale", "1"}});
        EXPECT_FALSE(edited.has_value());
    }

    TEST(Displays, PlansOnlyWhatChanged) {
        static std::atomic<int> counter = 0;
        const auto root = std::filesystem::temp_directory_path() / ("lantern-hyprland-displays-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
        std::filesystem::create_directories(root);
        const auto lua = root / "hyprland.lua";
        std::ofstream{lua} << config;
        const configuration::MainConfig main{.file = lua, .choice = configuration::Choice::search, .safeMode = false};
        const auto                      displays = *desktop::parseDisplays(reply);

        std::vector<desktop::DisplaySettings> wanted{displays[0].settings, displays[1].settings};
        const auto                            same = desktop::planDisplays(displays, wanted, std::nullopt, main);
        ASSERT_TRUE(same.has_value()) << same.error();
        EXPECT_FALSE(same->has_value());

        // The Samsung, matched by no call but the catch-all, gets one of its own by its
        // description; the laptop's call is changed where it is.
        wanted[0].mode    = {3840, 2160, 29.97};
        wanted[1].enabled = false;
        const auto planned = desktop::planDisplays(displays, wanted, std::nullopt, main);
        ASSERT_TRUE(planned.has_value() && planned->has_value());
        const auto& text = (*planned)->proposed;
        EXPECT_TRUE(text.contains("output   = \"desc:Samsung Electric Company Example 27 0000001\",\n    mode     = \"3840x2160@29.97\",\n    position = \"0x0\",\n    scale    = 1,\n})"))
            << text;
        EXPECT_TRUE(text.contains("hl.monitor({ output = \"eDP-1\", mode = \"preferred\", disabled = true })")) << text;
        std::filesystem::remove_all(root);
    }

    TEST(Displays, KeepsX11AppsSharp) {
        static std::atomic<int> counter = 0;
        const auto root = std::filesystem::temp_directory_path() / ("lantern-hyprland-sharp-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
        std::filesystem::create_directories(root);
        const auto lua = root / "hyprland.lua";
        std::ofstream{lua} << config;
        const configuration::MainConfig main{.file = lua, .choice = configuration::Choice::search, .safeMode = false};
        const auto                      displays = *desktop::parseDisplays(reply);
        std::vector<desktop::DisplaySettings> wanted{displays[0].settings, displays[1].settings};

        EXPECT_EQ(desktop::currentSharpApps(main), desktop::SharpApps{});
        // At 1.25 GTK cannot scale, so only Hyprland's setting is written; at 2, GTK's too.
        EXPECT_EQ(desktop::sharpAppsFor(true, wanted), (desktop::SharpApps{.on = true, .gtkScale = std::nullopt}));
        wanted[0].scale = 2;
        const auto sharp = desktop::sharpAppsFor(true, wanted);
        EXPECT_EQ(sharp, (desktop::SharpApps{.on = true, .gtkScale = 2}));
        wanted[0].scale = 1;
        const auto planned = desktop::planDisplays(displays, wanted, sharp, main);
        ASSERT_TRUE(planned.has_value() && planned->has_value());
        EXPECT_TRUE((*planned)->proposed.ends_with("\nhl.config({ xwayland = { force_zero_scaling = true } })\nhl.env(\"GDK_SCALE\", \"2\")\n")) << (*planned)->proposed;
        ASSERT_TRUE(configuration::applyChanges({**planned}, root / "backups", "Displays").has_value());
        EXPECT_EQ(desktop::currentSharpApps(main), sharp);

        // Turned off, GTK's scale goes back to 1 rather than doubling its apps.
        const auto off = desktop::planDisplays(displays, wanted, desktop::SharpApps{}, main);
        ASSERT_TRUE(off.has_value() && off->has_value());
        EXPECT_TRUE((*off)->proposed.contains("force_zero_scaling = false"));
        EXPECT_TRUE((*off)->proposed.contains("hl.env(\"GDK_SCALE\", \"1\")"));
        std::filesystem::remove_all(root);
    }

}
