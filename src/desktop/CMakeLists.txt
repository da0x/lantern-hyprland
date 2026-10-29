# SPDX-License-Identifier: GPL-3.0-or-later
pkg_check_modules(fontconfig REQUIRED IMPORTED_TARGET fontconfig)
find_package(nlohmann_json 3.11 REQUIRED)

add_library(lantern-hyprland-desktop STATIC accessibility.cpp autostart-entries.cpp default-apps.cpp fastfetch.cpp popups.cpp screenshots.cpp trash.cpp user-themes.cpp devices.cpp displays.cpp essentials.cpp idle.cpp night-light.cpp input.cpp window-look.cpp clipboard.cpp process.cpp color.cpp cursor-theme.cpp gsettings.cpp system.cpp terminal-theme.cpp terminal-themes.cpp theme.cpp wallpaper.cpp waybar-style.cpp waybar.cpp)
target_include_directories(lantern-hyprland-desktop PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(lantern-hyprland-desktop PUBLIC lantern-hyprland-configuration lantern-hyprland-foundation PRIVATE PkgConfig::fontconfig nlohmann_json::nlohmann_json lantern-hyprland-syntax)
# terminal-themes.cpp embeds the bundled themes; rebuild it when one changes.
file(GLOB terminalThemeFiles CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/terminal-themes/*.conf)
set_property(SOURCE terminal-themes.cpp APPEND PROPERTY OBJECT_DEPENDS ${terminalThemeFiles})
