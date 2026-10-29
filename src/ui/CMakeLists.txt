# SPDX-License-Identifier: GPL-3.0-or-later
add_library(lantern-hyprland-ui STATIC accessibility-page.cpp autostart-page.cpp system-info-page.cpp tabbed-page.cpp default-apps-page.cpp screenshots-page.cpp console-page.cpp devices-page.cpp displays-page.cpp essentials-page.cpp idle-page.cpp night-light-page.cpp input-page.cpp cursors-page.cpp event-log-page.cpp live-tree-page.cpp options-page.cpp activate-page.cpp choice-cards.cpp desktop-preview.cpp windows-page.cpp bar-page.cpp bar-preview.cpp keybindings-page.cpp main-window.cpp markup.cpp program-switch.cpp restyle.cpp row-list.cpp source-view.cpp terminal-page.cpp texts.cpp theme-page.cpp typing.cpp wallpaper-page.cpp widgets.cpp)
target_include_directories(lantern-hyprland-ui PUBLIC ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR}/generated)

# Whether hyprtoolkit's scroll area can be used; see toolkit.hpp.in.
if(LANTERN_HYPRLAND_FIXED_HYPRTOOLKIT)
    set(LANTERN_HYPRLAND_FIXED_HYPRTOOLKIT_VALUE true)
else()
    set(LANTERN_HYPRLAND_FIXED_HYPRTOOLKIT_VALUE false)
endif()
configure_file(toolkit.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/ui/toolkit.hpp @ONLY)
target_link_libraries(lantern-hyprland-ui PUBLIC lantern-hyprland-application PRIVATE lantern-hyprland-activation PkgConfig::hyprtoolkit lantern-hyprland-desktop lantern-hyprland-syntax)
