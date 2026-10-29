// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation/effects.hpp"
#include "activation/activator.hpp"
#include "activation/record.hpp"
#include "desktop/process.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

namespace {

    TEST(Activation, SortsEffectsIntoAreasAndSaysWhatItLeavesOut) {
        const auto text = std::string{"-- mine\n"
                                      "hl.config({ decoration = { rounding = 16 } })\n"
                                      "hl.window_rule({ name = \"x\", match = { class = \"y\" }, float = true })\n"
                                      "hl.bind(\"SUPER + SHIFT + K\", hl.dsp.exec_cmd(\"kitty\"))\n"
                                      "hl.exec_cmd(\"waybar -c ~/bar.jsonc\")\n"
                                      "hl.env(\"XCURSOR_SIZE\", \"32\")\n"
                                      "local x = 1\n"};
        const auto read = activation::readEffects(text);
        ASSERT_EQ(read.effects.size(), 4U);
        EXPECT_EQ(read.in(activation::Area::options).size(), 2U);
        ASSERT_EQ(read.in(activation::Area::keybindings).size(), 1U);
        EXPECT_EQ(read.in(activation::Area::keybindings)[0]->keys, "SUPER + SHIFT + K");
        ASSERT_EQ(read.in(activation::Area::programs).size(), 1U);
        EXPECT_EQ(read.in(activation::Area::programs)[0]->command, "waybar -c ~/bar.jsonc");
        ASSERT_EQ(read.skipped.size(), 2U);
        EXPECT_EQ(read.skipped[0].line, 6);
        EXPECT_EQ(activation::luaFor(read, activation::Area::options),
                  "hl.config({ decoration = { rounding = 16 } })\nhl.window_rule({ name = \"x\", match = { class = \"y\" }, float = true })\n");
        // The template is all comments: nothing to do until the user says so.
        EXPECT_TRUE(activation::readEffects(activation::effectsTemplate()).effects.empty());
        EXPECT_TRUE(activation::readEffects(activation::effectsTemplate()).skipped.empty());
    }

    TEST(Activation, KeepsTheRecordUntilItIsRemoved) {
        static std::atomic<int> counter = 0;
        const auto folder = std::filesystem::temp_directory_path() / ("lantern-hyprland-activation-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
        EXPECT_FALSE(activation::loadRecord(folder).has_value());
        const activation::Record record{.areas = {activation::Area::options, activation::Area::programs},
                                        .programs = {{.process = 4242, .name = "waybar", .command = "waybar"}},
                                        .since = "09:30"};
        ASSERT_TRUE(activation::saveRecord(record, folder).has_value());
        const auto loaded = activation::loadRecord(folder);
        ASSERT_TRUE(loaded.has_value());
        EXPECT_EQ(loaded->areas, record.areas);
        ASSERT_EQ(loaded->programs.size(), 1U);
        EXPECT_EQ(loaded->programs[0].process, 4242);
        EXPECT_EQ(loaded->programs[0].name, "waybar");
        EXPECT_EQ(loaded->since, "09:30");
        activation::removeRecord(folder);
        EXPECT_FALSE(activation::loadRecord(folder).has_value());
        std::filesystem::remove_all(folder);
    }

    TEST(Activation, StartsAndStopsOnlyItsOwnPrograms) {
        const auto effects = activation::readEffects("hl.exec_cmd(\"sleep 30\")\nhl.config({ general = { gaps_in = 3 } })\n");
        // Options are allowed but there is no hyprctl: said, and left out of the record, since
        // nothing reached Hyprland to take back.
        const auto done = activation::activate(effects, {activation::Area::options, activation::Area::programs}, std::nullopt, "09:30");
        ASSERT_EQ(done.problems.size(), 1U);
        EXPECT_EQ(done.record.areas, (std::vector{activation::Area::programs}));
        ASSERT_EQ(done.record.programs.size(), 1U);
        const auto& sleeping = done.record.programs[0];
        EXPECT_EQ(sleeping.name, "sleep");
        // Until the shell has become the program, the process has the shell's name.
        for (int tries = 0; tries < 100 && desktop::processName(sleeping.process) != "sleep"; ++tries)
            ::usleep(10000);

        // A process by that number with another name is someone else's, and left alone.
        static std::atomic<int> counter = 0;
        const auto fake = std::filesystem::temp_directory_path() / ("lantern-hyprland-proc-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
        std::filesystem::create_directories(fake / std::to_string(sleeping.process));
        std::ofstream{fake / std::to_string(sleeping.process) / "comm"} << "firefox\n";
        EXPECT_EQ(activation::deactivate({.areas = {activation::Area::programs}, .programs = {sleeping}, .since = {}}, std::nullopt, fake).size(), 0U);
        EXPECT_EQ(desktop::processName(sleeping.process).value_or(""), "sleep");
        std::filesystem::remove_all(fake);

        // The real one is stopped.
        EXPECT_TRUE(activation::deactivate(done.record, std::nullopt).empty());
        for (int tries = 0; tries < 100 && desktop::processName(sleeping.process) == "sleep"; ++tries)
            ::usleep(10000);
        // Stopped: gone, or a zombie that no longer runs sleep.
        const auto state = std::filesystem::exists(std::filesystem::path{"/proc"} / std::to_string(sleeping.process) / "status");
        if (state) {
            std::ifstream status{std::filesystem::path{"/proc"} / std::to_string(sleeping.process) / "status"};
            std::string   line;
            while (std::getline(status, line) && !line.starts_with("State:")) {}
            EXPECT_TRUE(line.contains("Z")) << line;
        }
    }

    TEST(Activation, FindsKeybindingsTheConfigAlreadyHas) {
        configuration::Bindings own;
        auto keys = configuration::parseKeys("SUPER + Return");
        ASSERT_TRUE(keys.has_value());
        own.bindings.push_back(configuration::Binding{.modifiers = keys->first, .key = keys->second, .submap = {}, .action = {}, .description = {},
                                                      .location = {.file = "/home/me/.config/hypr/hyprland.lua", .line = 42}});
        const auto effects = activation::readEffects("hl.bind(\"SUPER + return\", hl.dsp.exec_cmd(\"kitty\"))\nhl.bind(\"SUPER + K\", hl.dsp.exec_cmd(\"kitty\"))\n");
        const auto found   = activation::collisions(effects, own);
        ASSERT_EQ(found.size(), 1U);
        EXPECT_EQ(found[0], "SUPER + return is also bound in hyprland.lua:42");
    }

}
