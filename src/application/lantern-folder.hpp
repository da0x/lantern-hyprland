// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/environment.hpp"

#include <filesystem>

namespace application {

    // Everything Lantern keeps for itself: $XDG_CONFIG_HOME/lantern-hyprland, or
    // ~/.config/lantern-hyprland. Deleting it removes every trace of Lantern.
    [[nodiscard]] std::filesystem::path lanternFolder(const foundation::Environment& environment);

    // Moves the folder kept before the rename to Lantern, hyprland-studio beside it, to
    // lanternFolder, when there is nothing there yet. The backups and settings move with
    // it, so Undo keeps working.
    void moveOldFolder(const foundation::Environment& environment);

    // Where each change Lantern writes is recorded, with copies of the files it replaced.
    [[nodiscard]] inline std::filesystem::path backupsFolder(const foundation::Environment& environment) {
        return lanternFolder(environment) / "backups";
    }

}
