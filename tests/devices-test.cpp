// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/devices.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include <unistd.h>

namespace {

    TEST(Devices, ReadsTheVolumeAndTheOutput) {
        EXPECT_EQ(desktop::parseVolume("Volume: 0.83\n"), (desktop::Volume{83, false}));
        EXPECT_EQ(desktop::parseVolume("Volume: 0.40 [MUTED]\n"), (desktop::Volume{40, true}));
        EXPECT_FALSE(desktop::parseVolume("Translate ID error").has_value());
        EXPECT_EQ(desktop::parseNodeName("id 52, type PipeWire:Interface:Node\n  * node.description = \"HDMI Digital Stereo\"\n  * node.nick = \"Example 27\"\n"),
                  "Example 27");
        EXPECT_EQ(desktop::parseNodeName("  * node.description = \"Built-in Audio\"\n"), "Built-in Audio");
    }

    TEST(Devices, ReadsTheConnectionsInUse) {
        const auto connections = desktop::parseConnections("Wired connection 2:802-3-ethernet:enp12s0\ndocker0:bridge:docker0\nlo:loopback:lo\nCafe\\: Guest:802-11-wireless:wlan0\n");
        ASSERT_EQ(connections.size(), 2U);
        EXPECT_EQ(connections[0], (desktop::Connection{"Wired connection 2", "wired", "enp12s0"}));
        EXPECT_EQ(connections[1], (desktop::Connection{"Cafe: Guest", "Wi-Fi", "wlan0"}));
    }

    TEST(Devices, FindsBluetoothAdapters) {
        const auto root = std::filesystem::temp_directory_path() / ("lantern-hyprland-bluetooth-" + std::to_string(::getpid()));
        std::filesystem::create_directories(root / "hci0");
        std::filesystem::create_directories(root / "hci0:256");
        EXPECT_EQ(desktop::bluetoothAdapters(root), std::vector<std::string>{"hci0"});
        EXPECT_TRUE(desktop::bluetoothAdapters(root / "missing").empty());
        std::filesystem::remove_all(root);
    }

}
