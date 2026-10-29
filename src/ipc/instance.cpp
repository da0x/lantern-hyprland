// SPDX-License-Identifier: GPL-3.0-or-later
#include "ipc/instance.hpp"

#include <cstdlib>
#include <string_view>

namespace ipc {

    std::expected<Instance, std::string> currentInstance() {
        const char* signature = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
        if (signature == nullptr || std::string_view{signature}.empty())
            return std::unexpected{"HYPRLAND_INSTANCE_SIGNATURE is not set, so this is not running under Hyprland"};

        const char* runtime = std::getenv("XDG_RUNTIME_DIR");
        if (runtime == nullptr || std::string_view{runtime}.empty())
            return std::unexpected{"XDG_RUNTIME_DIR is not set"};

        const auto folder = std::filesystem::path{runtime} / "hypr" / signature;
        return Instance{.folder = folder, .requestSocket = folder / ".socket.sock", .eventSocket = folder / ".socket2.sock"};
    }

}
