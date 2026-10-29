// SPDX-License-Identifier: GPL-3.0-or-later
#include "ipc/launch.hpp"
#include "ipc/status.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

TEST(Launch, ReadsAProcessFromProc) {
    const auto launch = ipc::readLaunch(::getpid());
    ASSERT_TRUE(launch) << launch.error();
    EXPECT_EQ(launch->workingFolder, std::filesystem::current_path());
    const char* home = std::getenv("HOME");
    ASSERT_NE(home, nullptr);
    EXPECT_EQ(launch->environment.at("HOME"), home);
}

TEST(Launch, FindsHyprlandsProcessInItsLockFile) {
    const auto folder = std::filesystem::temp_directory_path() / ("lantern-hyprland-lock-" + std::to_string(::getpid()));
    std::filesystem::create_directories(folder);
    const ipc::Instance instance{.folder = folder, .requestSocket = folder / ".socket.sock", .eventSocket = folder / ".socket2.sock"};

    std::ofstream{folder / "hyprland.lock"} << "4212\nwayland-1\n";
    EXPECT_EQ(ipc::processOf(instance), 4212);

    std::ofstream{folder / "hyprland.lock"} << "wayland-1\n";
    EXPECT_FALSE(ipc::processOf(instance));

    std::filesystem::remove_all(folder);
    EXPECT_FALSE(ipc::processOf(instance));
}

TEST(Status, ReadsTheConfigLanguage) {
    EXPECT_EQ(ipc::parseConfigFormat(R"({"configProvider": "lua", "backend": "drm"})"), ipc::ConfigFormat::lua);
    EXPECT_EQ(ipc::parseConfigFormat(R"({"configProvider": "hyprlang", "backend": "drm"})"), ipc::ConfigFormat::hyprlang);
    EXPECT_EQ(ipc::parseConfigFormat(R"({"backend": "drm"})"), ipc::ConfigFormat::unknown);
    EXPECT_FALSE(ipc::parseConfigFormat("not json"));
}
