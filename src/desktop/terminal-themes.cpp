// SPDX-License-Identifier: GPL-3.0-or-later
// The bundled terminal themes. Each file is kitty-themes' own, unchanged; see
// terminal-themes/NOTICE.
#include "desktop/terminal-theme.hpp"

#include <array>
#include <span>
#include <string_view>
#include <vector>

namespace desktop {

    namespace {

        constexpr unsigned char file_Alabaster[] = {
#embed "terminal-themes/Alabaster.conf"
        };
        constexpr unsigned char file_BlackMetal[] = {
#embed "terminal-themes/BlackMetal.conf"
        };
        constexpr unsigned char file_Catppuccin_Frappe[] = {
#embed "terminal-themes/Catppuccin-Frappe.conf"
        };
        constexpr unsigned char file_Catppuccin_Latte[] = {
#embed "terminal-themes/Catppuccin-Latte.conf"
        };
        constexpr unsigned char file_Catppuccin_Macchiato[] = {
#embed "terminal-themes/Catppuccin-Macchiato.conf"
        };
        constexpr unsigned char file_Catppuccin_Mocha[] = {
#embed "terminal-themes/Catppuccin-Mocha.conf"
        };
        constexpr unsigned char file_Dracula[] = {
#embed "terminal-themes/Dracula.conf"
        };
        constexpr unsigned char file_everforest_dark_medium[] = {
#embed "terminal-themes/everforest_dark_medium.conf"
        };
        constexpr unsigned char file_everforest_light_medium[] = {
#embed "terminal-themes/everforest_light_medium.conf"
        };
        constexpr unsigned char file_GitHub_Dark[] = {
#embed "terminal-themes/GitHub_Dark.conf"
        };
        constexpr unsigned char file_GitHub_Dark_Colorblind[] = {
#embed "terminal-themes/GitHub_Dark_Colorblind.conf"
        };
        constexpr unsigned char file_GitHub_Dark_High_Contrast[] = {
#embed "terminal-themes/GitHub_Dark_High_Contrast.conf"
        };
        constexpr unsigned char file_GitHub_Light[] = {
#embed "terminal-themes/GitHub_Light.conf"
        };
        constexpr unsigned char file_GitHub_Light_Colorblind[] = {
#embed "terminal-themes/GitHub_Light_Colorblind.conf"
        };
        constexpr unsigned char file_gruvbox_dark[] = {
#embed "terminal-themes/gruvbox-dark.conf"
        };
        constexpr unsigned char file_gruvbox_light[] = {
#embed "terminal-themes/gruvbox-light.conf"
        };
        constexpr unsigned char file_kanagawa[] = {
#embed "terminal-themes/kanagawa.conf"
        };
        constexpr unsigned char file_kanagawa_dragon[] = {
#embed "terminal-themes/kanagawa_dragon.conf"
        };
        constexpr unsigned char file_kanagawa_light[] = {
#embed "terminal-themes/kanagawa_light.conf"
        };
        constexpr unsigned char file_Modus_Operandi[] = {
#embed "terminal-themes/Modus_Operandi.conf"
        };
        constexpr unsigned char file_Modus_Vivendi[] = {
#embed "terminal-themes/Modus_Vivendi.conf"
        };
        constexpr unsigned char file_Nightfox[] = {
#embed "terminal-themes/Nightfox.conf"
        };
        constexpr unsigned char file_Nord[] = {
#embed "terminal-themes/Nord.conf"
        };
        constexpr unsigned char file_OneHalfLight[] = {
#embed "terminal-themes/OneHalfLight.conf"
        };
        constexpr unsigned char file_PaperColor_dark[] = {
#embed "terminal-themes/PaperColor_dark.conf"
        };
        constexpr unsigned char file_PaperColor_light[] = {
#embed "terminal-themes/PaperColor_light.conf"
        };
        constexpr unsigned char file_rose_pine[] = {
#embed "terminal-themes/rose-pine.conf"
        };
        constexpr unsigned char file_rose_pine_dawn[] = {
#embed "terminal-themes/rose-pine-dawn.conf"
        };
        constexpr unsigned char file_rose_pine_moon[] = {
#embed "terminal-themes/rose-pine-moon.conf"
        };
        constexpr unsigned char file_Sequoia_Monochrome_Dark[] = {
#embed "terminal-themes/Sequoia_Monochrome_Dark.conf"
        };
        constexpr unsigned char file_Solarized_Dark[] = {
#embed "terminal-themes/Solarized_Dark.conf"
        };
        constexpr unsigned char file_Solarized_Light[] = {
#embed "terminal-themes/Solarized_Light.conf"
        };
        constexpr unsigned char file_tokyo_night_night[] = {
#embed "terminal-themes/tokyo_night_night.conf"
        };
        constexpr unsigned char file_tokyo_night_day[] = {
#embed "terminal-themes/tokyo_night_day.conf"
        };
        constexpr unsigned char file_tokyo_night_storm[] = {
#embed "terminal-themes/tokyo_night_storm.conf"
        };

        struct Bundled {
            std::string_view               name;
            std::span<const unsigned char> file;
        };

        constexpr std::array<Bundled, 35> bundled{{
            {"Alabaster", file_Alabaster},
            {"Black Metal", file_BlackMetal},
            {"Catppuccin Frappé", file_Catppuccin_Frappe},
            {"Catppuccin Latte", file_Catppuccin_Latte},
            {"Catppuccin Macchiato", file_Catppuccin_Macchiato},
            {"Catppuccin Mocha", file_Catppuccin_Mocha},
            {"Dracula", file_Dracula},
            {"Everforest Dark", file_everforest_dark_medium},
            {"Everforest Light", file_everforest_light_medium},
            {"GitHub Dark", file_GitHub_Dark},
            {"GitHub Dark Colorblind", file_GitHub_Dark_Colorblind},
            {"GitHub Dark High Contrast", file_GitHub_Dark_High_Contrast},
            {"GitHub Light", file_GitHub_Light},
            {"GitHub Light Colorblind", file_GitHub_Light_Colorblind},
            {"Gruvbox Dark", file_gruvbox_dark},
            {"Gruvbox Light", file_gruvbox_light},
            {"Kanagawa", file_kanagawa},
            {"Kanagawa Dragon", file_kanagawa_dragon},
            {"Kanagawa Light", file_kanagawa_light},
            {"Modus Operandi", file_Modus_Operandi},
            {"Modus Vivendi", file_Modus_Vivendi},
            {"Nightfox", file_Nightfox},
            {"Nord", file_Nord},
            {"One Half Light", file_OneHalfLight},
            {"PaperColor Dark", file_PaperColor_dark},
            {"PaperColor Light", file_PaperColor_light},
            {"Rosé Pine", file_rose_pine},
            {"Rosé Pine Dawn", file_rose_pine_dawn},
            {"Rosé Pine Moon", file_rose_pine_moon},
            {"Sequoia Monochrome", file_Sequoia_Monochrome_Dark},
            {"Solarized Dark", file_Solarized_Dark},
            {"Solarized Light", file_Solarized_Light},
            {"Tokyo Night", file_tokyo_night_night},
            {"Tokyo Night Day", file_tokyo_night_day},
            {"Tokyo Night Storm", file_tokyo_night_storm},
        }};

    }

    std::span<const TerminalTheme> terminalThemes() {
        static const auto themes = [] {
            std::vector<TerminalTheme> made;
            for (const auto& [name, file] : bundled)
                made.push_back(terminalTheme(name, std::string_view{reinterpret_cast<const char*>(file.data()), file.size()})); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): bytes read as text
            return made;
        }();
        return themes;
    }

}
