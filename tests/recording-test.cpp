// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/bindings.hpp"
#include "configuration/recording.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

namespace {

    const std::filesystem::path fixture = std::filesystem::path{FIXTURES} / "lua-config" / "hyprland.lua";

    // A config folder of its own for one test, removed afterwards.
    class Scratch {
      public:
        explicit Scratch(const std::string& config) {
            static std::atomic<int> counter = 0;
            folder = std::filesystem::temp_directory_path() / ("lantern-hyprland-test-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
            std::filesystem::create_directories(folder);
            std::ofstream{folder / "hyprland.lua"} << config;
        }
        ~Scratch() { std::filesystem::remove_all(folder); }
        Scratch(const Scratch&)            = delete;
        Scratch& operator=(const Scratch&) = delete;

        [[nodiscard]] configuration::Recording record() const {
            auto recording = configuration::record(folder / "hyprland.lua");
            EXPECT_TRUE(recording) << recording.error();
            return recording ? *recording : configuration::Recording{};
        }

        std::filesystem::path folder;
    };

    // The value of hl.env(name, value), which these tests use to report back.
    std::string reported(const configuration::Recording& recording, const std::string& name) {
        for (const auto& call : recording.calls)
            if (call.function == "env" && call.arguments.size() == 2 && call.arguments[0].string() != nullptr && *call.arguments[0].string() == name)
                return call.arguments[1].string() != nullptr ? *call.arguments[1].string() : call.arguments[1].lua();
        return "<not reported>";
    }

}

TEST(Recording, ReadsEveryBindingWithItsActionAndLine) {
    const auto recording = configuration::record(fixture);
    ASSERT_TRUE(recording) << recording.error();
    ASSERT_FALSE(recording->failure) << recording->failure->message;

    const auto result = configuration::bindings(*recording);
    EXPECT_TRUE(result.unreadable.empty());
    ASSERT_EQ(result.bindings.size(), 10U);

    const auto& quit = result.bindings[0];
    EXPECT_EQ(quit.modifiers.with(quit.key), "SUPER + Q");
    EXPECT_EQ(quit.action.lua(), R"(exec_cmd("kitty"))");
    EXPECT_EQ(quit.location.brief(), "hyprland.lua:16");

    EXPECT_TRUE(result.bindings[1].mouse);
    EXPECT_EQ(result.bindings[1].action.lua(), "window.drag()");

    // Made in a loop: each one has its own keys and the loop's line.
    EXPECT_EQ(result.bindings[4].modifiers.with(result.bindings[4].key), "SUPER + 3");
    EXPECT_EQ(result.bindings[4].action.lua(), "focus({ workspace = 3 })");
    EXPECT_EQ(result.bindings[4].location.brief(), "hyprland.lua:20");

    EXPECT_EQ(result.bindings[5].action.lua(), "function (hyprland.lua:23)");

    EXPECT_EQ(result.bindings[6].description, "Mute");
    EXPECT_TRUE(result.bindings[6].locked);

    // Chosen from a file the config read at load time.
    EXPECT_EQ(result.bindings[7].modifiers.with(result.bindings[7].key), "SUPER + A");

    EXPECT_EQ(result.bindings[8].submap, "resize");
    EXPECT_TRUE(result.bindings[8].repeat);
    EXPECT_EQ(result.bindings[8].location.brief(), "resize.lua:3");
    EXPECT_EQ(result.bindings[9].action.lua(), R"(submap("reset"))");
}

TEST(Recording, RunsNothingAndSaysWhatItRefused) {
    const auto recording = configuration::record(fixture);
    ASSERT_TRUE(recording) << recording.error();

    // The startup function was recorded, never called: waybar was not "started".
    EXPECT_FALSE(std::ranges::any_of(recording->calls, [](const auto& call) { return call.function == "exec_cmd"; }));

    ASSERT_EQ(recording->refused.size(), 1U);
    EXPECT_EQ(recording->refused[0].what, "os.execute");
    EXPECT_FALSE(std::filesystem::exists(fixture.parent_path() / "should-never-exist"));
    EXPECT_EQ(recording->files.size(), 2U);
}

TEST(Sandbox, ReadsOnlyInsideTheConfigFolder) {
    const Scratch scratch{R"(
        local outside, error = io.open("/etc/hostname")
        hl.env("outside", tostring(outside))
        hl.env("parent", tostring((io.open("../hyprland.lua"))))
        hl.env("self", tostring(io.open("hyprland.lua") ~= nil))
    )"};
    const auto recording = scratch.record();
    EXPECT_EQ(reported(recording, "outside"), "nil");
    EXPECT_EQ(reported(recording, "parent"), "nil");
    EXPECT_EQ(reported(recording, "self"), "true");
}

TEST(Sandbox, RefusesWritesAndProcesses) {
    const Scratch scratch{R"(
        hl.env("write", tostring((io.open("written", "w"))))
        hl.env("popen", tostring((io.popen("true"))))
    )"};
    const auto recording = scratch.record();
    EXPECT_EQ(reported(recording, "write"), "nil");
    EXPECT_EQ(reported(recording, "popen"), "nil");
    EXPECT_EQ(recording.refused.size(), 2U);
    EXPECT_FALSE(std::filesystem::exists(scratch.folder / "written"));
}

TEST(Sandbox, LoadCannotReachTheInterpretersOwnLibraries) {
    const Scratch scratch{R"(
        hl.env("debug", tostring(load("return debug")()))
        hl.env("package", tostring(load("return package")()))
        hl.env("dump", tostring(("").dump))
        hl.env("strings", tostring(getmetatable("")))
        hl.env("binary", tostring((load("\27Lua"))))
    )"};
    const auto recording = scratch.record();
    EXPECT_EQ(reported(recording, "debug"), "nil");
    EXPECT_EQ(reported(recording, "package"), "nil");
    EXPECT_EQ(reported(recording, "dump"), "nil");
    EXPECT_EQ(reported(recording, "strings"), "nil");
    EXPECT_EQ(reported(recording, "binary"), "nil");
}

TEST(Sandbox, StopsARunawayConfig) {
    const Scratch scratch{"hl.env('before', 'yes')\nwhile true do end\n"};
    const auto    recording = scratch.record();
    ASSERT_TRUE(recording.failure);
    EXPECT_NE(recording.failure->message.find("stopped after"), std::string::npos) << recording.failure->message;
    EXPECT_EQ(reported(recording, "before"), "yes");
}

TEST(Sandbox, ReportsACallOutsideHyprlandsApi) {
    const Scratch scratch{"hl.no_such_call()\n"};
    const auto    recording = scratch.record();
    ASSERT_TRUE(recording.failure);
    EXPECT_NE(recording.failure->message.find("hl.no_such_call is not part"), std::string::npos) << recording.failure->message;
}

TEST(Keys, ReadsModifiersInAnyCaseAndTheKey) {
    const auto keys = configuration::parseKeys("super + Shift +S");
    ASSERT_TRUE(keys) << keys.error();
    EXPECT_EQ(keys->first.with(keys->second), "SUPER + SHIFT + S");

    const auto plus = configuration::parseKeys("CTRL + +");
    ASSERT_TRUE(plus) << plus.error();
    EXPECT_EQ(plus->second, "+");

    EXPECT_FALSE(configuration::parseKeys("HYPER + Q"));
    EXPECT_FALSE(configuration::parseKeys("SUPER + "));
}
