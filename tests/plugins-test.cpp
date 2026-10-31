// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/plugins.hpp"
#include "ipc/plugins.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include <unistd.h>

namespace {

    application::Plugin notLoaded(std::string name, std::filesystem::path file) {
        application::Plugin plugin;
        plugin.name = std::move(name);
        plugin.file = std::move(file);
        return plugin;
    }

    configuration::Call call(std::string function, std::vector<configuration::Value> arguments) {
        configuration::Call made;
        made.function  = std::move(function);
        made.arguments = std::move(arguments);
        return made;
    }

    ipc::Instance instanceIn(std::filesystem::path folder) {
        ipc::Instance instance;
        instance.folder = std::move(folder);
        return instance;
    }

    TEST(Plugins, ReadsHyprlandsPluginList) {
        const auto plugins = ipc::parseLoadedPlugins(R"([{"name": "hyprexpo", "author": "hyprwm", "handle": "5617a7c3d8f0", "version": "1.0", "description": "A plugin for an overview"}])");
        ASSERT_TRUE(plugins.has_value());
        EXPECT_EQ(*plugins, (std::vector<ipc::LoadedPlugin>{{"hyprexpo", "hyprwm", "1.0", "A plugin for an overview"}}));
        EXPECT_EQ(ipc::parseLoadedPlugins("no plugins loaded"), std::vector<ipc::LoadedPlugin>{});
        EXPECT_FALSE(ipc::parseLoadedPlugins("unknown request").has_value());
    }

    TEST(Plugins, FindsTheLibrariesHyprlandMapped) {
        const auto root = std::filesystem::temp_directory_path() / ("lantern-hyprland-plugins-" + std::to_string(::getpid()));
        std::filesystem::create_directories(root / "instance");
        std::filesystem::create_directories(root / "proc" / "4242");
        std::ofstream{root / "instance" / "hyprland.lock"} << "4242\nwayland-1\n";
        std::ofstream{root / "proc" / "4242" / "maps"} << "55d0c0a00000-55d0c0a01000 r--p 00000000 08:03 11 /usr/bin/Hyprland\n"
                                                          "7f0000000000-7f0000001000 r--p 00000000 08:03 12 /home/me/plugins/hyprexpo.so\n"
                                                          "7f0000001000-7f0000002000 r-xp 00001000 08:03 12 /home/me/plugins/hyprexpo.so\n"
                                                          "7f0000003000-7f0000004000 r--p 00000000 08:03 13 /usr/lib/libc.so.6\n"
                                                          "7f0000005000-7f0000006000 rw-p 00000000 00:00 0 \n"
                                                          "7ffd00000000-7ffd00001000 rw-p 00000000 00:00 0 [stack]\n";
        EXPECT_EQ(ipc::mappedLibraries(instanceIn(root / "instance"), root / "proc"), (std::vector<std::filesystem::path>{"/home/me/plugins/hyprexpo.so", "/usr/lib/libc.so.6"}));
        EXPECT_TRUE(ipc::mappedLibraries(instanceIn(root / "missing"), root / "proc").empty());
        std::filesystem::remove_all(root);
    }

    TEST(Plugins, MatchesEachLoadedPluginToItsFile) {
        const std::vector<ipc::LoadedPlugin>      loaded{{"hyprexpo", "hyprwm", "1.0", "An overview"}, {"Hypr-Bars", "", "", ""}, {"mystery", "", "", ""}};
        const std::vector<std::filesystem::path> mapped{"/usr/lib/libc.so.6", "/tmp/old/hyprexpo.so", "/home/me/plugins/hyprexpo.so", "/home/me/plugins/libhyprbars.so"};
        const std::vector<std::filesystem::path> configured{"/home/me/plugins/hyprexpo.so"};
        const auto                               plugins = application::listPlugins(loaded, mapped, configured, {});
        ASSERT_EQ(plugins.size(), 3U);
        // The configured copy wins over another file of the same name.
        EXPECT_EQ(plugins[0].file, std::filesystem::path{"/home/me/plugins/hyprexpo.so"});
        EXPECT_TRUE(plugins[0].inConfig);
        EXPECT_TRUE(plugins[0].loaded);
        EXPECT_EQ(plugins[1].file, std::filesystem::path{"/home/me/plugins/libhyprbars.so"});
        EXPECT_FALSE(plugins[1].inConfig);
        EXPECT_FALSE(plugins[2].file.has_value());
    }

    TEST(Plugins, ListsTurnedOffPluginsThatStillExist) {
        const auto root = std::filesystem::temp_directory_path() / ("lantern-hyprland-turned-off-" + std::to_string(::getpid()));
        std::filesystem::create_directories(root);
        std::ofstream{root / "hypr3d.so"} << "";
        const std::vector<std::filesystem::path> configured{"/home/me/plugins/hyprexpo.so"};
        const std::vector<std::filesystem::path> remembered{root / "hypr3d.so", root / "gone.so"};
        const auto                               plugins = application::listPlugins({}, {}, configured, remembered);
        ASSERT_EQ(plugins.size(), 2U);
        auto inConfig     = notLoaded("hyprexpo", configured[0]);
        inConfig.inConfig = true;
        auto turnedOff       = notLoaded("hypr3d", root / "hypr3d.so");
        turnedOff.remembered = true;
        EXPECT_EQ(plugins[0], inConfig);
        EXPECT_EQ(plugins[1], turnedOff);
        std::filesystem::remove_all(root);
    }

    TEST(Plugins, ReadsThePluginsAConfigLoads) {
        configuration::Recording recording;
        recording.calls.push_back(call("plugin.load", {configuration::Value{std::string{"~/plugins/hyprexpo.so"}}}));
        recording.calls.push_back(call("config", {}));
        recording.calls.push_back(call("plugin.load", {configuration::Value{std::string{"/usr/lib/hyprbars.so"}}}));
        EXPECT_EQ(application::pluginsInConfig(recording, "/home/me"), (std::vector<std::filesystem::path>{"/home/me/plugins/hyprexpo.so", "/usr/lib/hyprbars.so"}));

        const auto conf = "plugin = ~/plugins/hyprexpo.so\n# plugin = /usr/lib/old.so\nplugin {\n    hyprexpo {\n        columns = 3\n    }\n}\n  plugin=/usr/lib/hyprbars.so # bars\n";
        EXPECT_EQ(application::pluginsInConfig(conf, "/home/me"), (std::vector<std::filesystem::path>{"/home/me/plugins/hyprexpo.so", "/usr/lib/hyprbars.so"}));
    }

}
