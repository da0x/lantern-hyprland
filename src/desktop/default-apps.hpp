// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/environment.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace desktop {

    // An installed application, from its .desktop file.
    struct Application {
        // Its file name, such as firefox.desktop, which mimeapps.list names it by.
        std::string              id;
        std::string              name;
        // The file types it says it opens.
        std::vector<std::string> types;
        // A terminal, by its categories.
        bool                     terminal = false;

        bool                     operator==(const Application&) const = default;
    };

    // A kind of app chosen once for many file types, such as the web browser.
    struct DefaultKind {
        std::string              name;
        std::string              about;
        // The first is the one whose default is shown.
        std::vector<std::string> types;
    };

    // The kinds the Default apps page offers, browser first.
    [[nodiscard]] std::span<const DefaultKind> defaultKinds();

    // $XDG_DATA_HOME/applications, then each of $XDG_DATA_DIRS'.
    [[nodiscard]] std::vector<std::filesystem::path> applicationFolders(const foundation::Environment& environment);

    // Every application in the folders, by its file name, the first folder's first, without
    // those marked Hidden, sorted by name.
    [[nodiscard]] std::vector<Application> applications(std::span<const std::filesystem::path> folders);

    // The mimeapps.list files that decide the defaults, the one that counts most first, as
    // the XDG specification orders them: for each config folder, the desktop's own and then
    // the shared one, then the same in the data folders' applications.
    [[nodiscard]] std::vector<std::filesystem::path> mimeappsFiles(const foundation::Environment& environment);

    // The file Lantern writes defaults to: the desktop's own in the user's config folder
    // when there is one, since it counts more, or else ~/.config/mimeapps.list.
    [[nodiscard]] std::filesystem::path userMimeappsFile(const foundation::Environment& environment);

    // The default for `type`: the first application named for it under [Default
    // Applications] in `lists` (their contents, the one that counts most first) that is
    // installed.
    [[nodiscard]] std::optional<std::string> defaultFor(std::span<const std::string> lists, std::string_view type, std::span<const Application> installed);

    // What opens `type` when no default is chosen: the first installed application in
    // [Added Associations] in `lists`, then in each folder's mimeinfo.cache, given in
    // `caches`, as GLib picks it.
    [[nodiscard]] std::optional<std::string> fallbackFor(std::span<const std::string> lists, std::span<const std::string> caches, std::string_view type,
                                                         std::span<const Application> installed);

    // `list` with `id` the default for each of `types`, first in each line, and every
    // other line kept; the group is added when there is none.
    [[nodiscard]] std::string withDefault(std::string_view list, std::span<const std::string> types, std::string_view id);

    // ~/.config/xdg-terminals.list, which xdg-terminal-exec reads, and GLib through it.
    [[nodiscard]] std::filesystem::path terminalsFile(const foundation::Environment& environment);

    // The first terminal it names.
    [[nodiscard]] std::optional<std::string> preferredTerminal(std::string_view list);

    // The list with `id` first, the rest kept.
    [[nodiscard]] std::string withTerminal(std::string_view list, std::string_view id);

}
