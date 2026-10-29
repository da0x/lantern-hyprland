// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/main-config.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

namespace {

    // A folder of its own for one test, removed afterwards, standing in for every folder
    // Hyprland looks in.
    class Folders {
      public:
        Folders() {
            static std::atomic<int> counter = 0;
            root = std::filesystem::temp_directory_path() / ("lantern-hyprland-main-config-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
            std::filesystem::create_directories(root);
        }
        ~Folders() { std::filesystem::remove_all(root); }
        Folders(const Folders&)            = delete;
        Folders& operator=(const Folders&) = delete;

        // Creates folder/hypr/name and returns its path.
        std::filesystem::path add(const std::string& folder, const std::string& name) const {
            const auto file = root / folder / "hypr" / name;
            std::filesystem::create_directories(file.parent_path());
            std::ofstream{file} << "-- test\n";
            return file;
        }

        // A launch whose search finds nothing outside these folders.
        [[nodiscard]] configuration::Launch launch() const {
            return configuration::Launch{
                .arguments     = {},
                .environment   = {{"HOME", (root / "home").string()}, {"XDG_CONFIG_DIRS", (root / "first").string() + ":" + (root / "second").string()}},
                .workingFolder = root,
            };
        }

        [[nodiscard]] std::filesystem::path system() const { return root / "system"; }

        std::filesystem::path root;
    };

}

TEST(MainConfig, FindsTheFirstLuaConfigBeforeAnyConf) {
    const Folders folders;
    folders.add("home/.config", "hyprland.conf");
    const auto lua = folders.add("second", "hyprland.lua");

    const auto found = configuration::mainConfig(folders.launch(), folders.system());
    ASSERT_TRUE(found) << found.error();
    EXPECT_EQ(found->file, lua);
    EXPECT_EQ(found->choice, configuration::Choice::search);
    EXPECT_TRUE(found->lua());
}

TEST(MainConfig, SearchesTheConfigHomeFirst) {
    const Folders folders;
    folders.add("home/.config", "hyprland.lua");
    const auto preferred = folders.add("config-home", "hyprland.lua");
    auto       launch    = folders.launch();
    launch.environment["XDG_CONFIG_HOME"] = (folders.root / "config-home").string();

    const auto found = configuration::mainConfig(launch, folders.system());
    ASSERT_TRUE(found) << found.error();
    EXPECT_EQ(found->file, preferred);
}

TEST(MainConfig, SkipsARelativeConfigHome) {
    const Folders folders;
    const auto    home = folders.add("home/.config", "hyprland.lua");
    folders.add("config-home", "hyprland.lua");
    auto launch                           = folders.launch();
    launch.environment["XDG_CONFIG_HOME"] = "config-home";

    const auto found = configuration::mainConfig(launch, folders.system());
    ASSERT_TRUE(found) << found.error();
    EXPECT_EQ(found->file, home);
}

TEST(MainConfig, FallsBackToAConf) {
    const Folders folders;
    const auto    conf  = folders.add("system", "hyprland.conf");
    const auto    found = configuration::mainConfig(folders.launch(), folders.system());
    ASSERT_TRUE(found) << found.error();
    EXPECT_EQ(found->file, conf);
    EXPECT_FALSE(found->lua());
}

TEST(MainConfig, PrefersTheCommandLineThenTheEnvironment) {
    const Folders folders;
    folders.add("home/.config", "hyprland.lua");
    const auto given    = folders.add("given", "hyprland.lua");
    const auto variable = folders.add("variable", "hyprland.lua");

    auto launch                            = folders.launch();
    launch.environment["HYPRLAND_CONFIG"]  = "variable/hypr/hyprland.lua";
    const auto fromEnvironment             = configuration::mainConfig(launch, folders.system());
    ASSERT_TRUE(fromEnvironment) << fromEnvironment.error();
    EXPECT_EQ(fromEnvironment->file, variable);
    EXPECT_EQ(fromEnvironment->choice, configuration::Choice::environment);

    // Relative to the folder Hyprland runs in, and after an option whose value looks like -c.
    launch.arguments           = {"--locked-cmd", "-c", "--config", "given/hypr/hyprland.lua"};
    const auto fromCommandLine = configuration::mainConfig(launch, folders.system());
    ASSERT_TRUE(fromCommandLine) << fromCommandLine.error();
    EXPECT_EQ(fromCommandLine->file, std::filesystem::canonical(given));
    EXPECT_EQ(fromCommandLine->choice, configuration::Choice::commandLine);
}

TEST(MainConfig, FollowsLinksOnlyInACommandLinePath) {
    const Folders folders;
    const auto    real = folders.add("real", "hyprland.lua");
    std::filesystem::create_directories(folders.root / "home" / ".config" / "hypr");
    const auto link = folders.root / "home" / ".config" / "hypr" / "hyprland.lua";
    std::filesystem::create_symlink(real, link);

    const auto searched = configuration::mainConfig(folders.launch(), folders.system());
    ASSERT_TRUE(searched) << searched.error();
    EXPECT_EQ(searched->file, link);

    auto launch      = folders.launch();
    launch.arguments = {"-c", link.string()};
    const auto given = configuration::mainConfig(launch, folders.system());
    ASSERT_TRUE(given) << given.error();
    EXPECT_EQ(given->file, std::filesystem::canonical(real));
}

TEST(MainConfig, NotesSafeMode) {
    const Folders folders;
    folders.add("home/.config", "hyprland.lua");
    auto launch      = folders.launch();
    launch.arguments = {"--safe-mode"};
    const auto found = configuration::mainConfig(launch, folders.system());
    ASSERT_TRUE(found) << found.error();
    EXPECT_TRUE(found->safeMode);
}

TEST(MainConfig, SaysWhenThereIsNoConfig) {
    const Folders folders;
    EXPECT_FALSE(configuration::mainConfig(folders.launch(), folders.system()));
}
