// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace desktop {

    // The default output's volume, from `wpctl get-volume`: "Volume: 0.83 [MUTED]".
    struct Volume {
        int  percent = 0;
        bool muted   = false;

        friend bool operator==(const Volume&, const Volume&) = default;
    };
    [[nodiscard]] std::optional<Volume> parseVolume(std::string_view output);

    // A device's short name from `wpctl inspect`: its nick, or else its description.
    [[nodiscard]] std::string parseNodeName(std::string_view output);

    // A network connection in use, from `nmcli -t -f NAME,TYPE,DEVICE connection show
    // --active`. The loopback and bridges, such as Docker's, are left out.
    struct Connection {
        std::string name;
        // "wired", "Wi-Fi", or NetworkManager's own word for another kind.
        std::string kind;
        std::string device;

        friend bool operator==(const Connection&, const Connection&) = default;
    };
    [[nodiscard]] std::vector<Connection> parseConnections(std::string_view output);

    // The Bluetooth adapters the kernel found, such as hci0.
    [[nodiscard]] std::vector<std::string> bluetoothAdapters(const std::filesystem::path& folder = "/sys/class/bluetooth");

}
