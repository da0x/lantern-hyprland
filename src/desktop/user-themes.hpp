// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "desktop/waybar-style.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace desktop {

    // A palette as the user edits it, each color as #rrggbb.
    struct ThemeDraft {
        std::string name;
        bool        dark = true;
        std::string base;
        std::string onAccent;
        std::string surface;
        std::string raised;
        std::string muted;
        std::string subtle;
        std::string text;
        std::string accent;
        std::string audio;
        std::string network;
        std::string alert;

        bool        operator==(const ThemeDraft&) const = default;
    };

    // Each color of a draft: its member, its name and what it colors.
    struct DraftColor {
        std::string ThemeDraft::* member;
        std::string_view          name;
        std::string_view          about;
    };
    [[nodiscard]] std::span<const DraftColor> draftColors();

    [[nodiscard]] ThemeDraft draftOf(const Palette& palette);

    // What keeps a draft from being saved, or from reading well, in sentences: a color
    // that is not #rrggbb, no name, or text too faint on the background. Empty when none.
    [[nodiscard]] std::vector<std::string> draftProblems(const ThemeDraft& draft);

    // The draft as its file holds it, a line for each value: name = Sunset.
    [[nodiscard]] std::string themeFileText(const ThemeDraft& draft);
    [[nodiscard]] std::optional<ThemeDraft> parseThemeFile(std::string_view text);

    // The nearest of GNOME's accent colors, by hue, or slate for a gray.
    [[nodiscard]] std::string_view gnomeAccentOf(std::string_view hex);

    // The built-in palettes and then the user's, by name. Each stays where it is for as
    // long as Lantern runs, also after the user's are read again, so a pointer to one
    // can be kept.
    [[nodiscard]] std::vector<const Palette*> allPalettes();

    // Reads the user's themes from `folder` again: every .conf file that parses.
    void loadUserPalettes(const std::filesystem::path& folder);

    // The file a palette of the user's was read from; nothing for a built-in one.
    [[nodiscard]] std::optional<std::filesystem::path> userThemeFile(const Palette* palette);

    // A file name for a new theme called `name` in `folder`, not taken by another.
    [[nodiscard]] std::filesystem::path newThemeFile(const std::filesystem::path& folder, std::string_view name);

}
