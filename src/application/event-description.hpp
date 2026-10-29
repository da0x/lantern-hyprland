// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ipc/events.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace application {

    // Says what an event means, in a sentence: "kitty opened on workspace 2". `windowName`
    // names a window by its address, when it is known.
    [[nodiscard]] std::string describeEvent(const ipc::Event& event, const std::function<std::optional<std::string>(std::string_view address)>& windowName);

    // Hyprland sends some events twice, the old form and a newer one with more in it. The
    // old one says nothing the new one does not, so the log leaves it out unless asked.
    [[nodiscard]] bool repeatsNewerEvent(std::string_view name);

}
