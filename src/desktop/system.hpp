// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace desktop {

    // The first executable file called `name` in the folders of `path`, a PATH value.
    [[nodiscard]] std::optional<std::filesystem::path> findProgram(std::string_view name, std::string_view path);

    // Whether fontconfig has a font of this family. It rescans the font folders first, so a
    // font installed while Lantern runs is found.
    [[nodiscard]] bool hasFontFamily(const std::string& family);

    // The families of the monospace fonts fontconfig has, rescanning the font folders first.
    // Symbol-only fonts, which have no letters, are left out.
    [[nodiscard]] std::vector<std::string> monospaceFamilies();

    // The Linux distribution, from os-release.
    struct Distribution {
        // Its display name, such as "Arch Linux".
        std::string              name;
        std::string              id;
        // The distributions it is derived from (ID_LIKE).
        std::vector<std::string> like;

        // Arch Linux or one built on it, whose packages and pacman Lantern can name.
        [[nodiscard]] bool       arch() const;
    };

    [[nodiscard]] Distribution parseOsRelease(std::string_view text);

    // From /etc/os-release, or /usr/lib/os-release where that is missing.
    [[nodiscard]] Distribution currentDistribution();

}
