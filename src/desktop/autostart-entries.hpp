// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/environment.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace desktop {

    // A program that asks to start with the desktop, from a .desktop file in an autostart
    // folder, as the XDG autostart specification has it.
    struct AutostartEntry {
        // Its file name, which a file of the same name in the user's folder overrides.
        std::string           id;
        std::string           name;
        std::string           command;
        // The file that counts: the user's own, or else the first system one.
        std::filesystem::path file;
        bool                  users = false;
        // Not Hidden, and not switched off as GNOME does it.
        bool                  on    = true;
        // Not limited by OnlyShowIn or NotShowIn to other desktops.
        bool                  forDesktop = true;

        bool                  operator==(const AutostartEntry&) const = default;
    };

    // ~/.config/autostart, or the one under $XDG_CONFIG_HOME.
    [[nodiscard]] std::filesystem::path userAutostartFolder(const foundation::Environment& environment);

    // The autostart folders of $XDG_CONFIG_DIRS, /etc/xdg/autostart when it is not set.
    [[nodiscard]] std::vector<std::filesystem::path> systemAutostartFolders(const foundation::Environment& environment);

    // Every entry in the folders, by its file name, the user's first; `desktops` is
    // $XDG_CURRENT_DESKTOP, a list separated by colons.
    [[nodiscard]] std::vector<AutostartEntry> autostartEntries(const std::filesystem::path& userFolder, std::span<const std::filesystem::path> systemFolders,
                                                               std::string_view desktops);

    // A .desktop file switched on or off: Hidden is set to false or true, and a GNOME
    // switch that is there is set to match. Every other line is kept.
    [[nodiscard]] std::string withEntryOn(std::string_view entry, bool on);

}
