# SPDX-License-Identifier: GPL-3.0-or-later
find_package(nlohmann_json 3.11 REQUIRED)

add_library(lantern-hyprland-activation STATIC activator.cpp effects.cpp record.cpp)
target_include_directories(lantern-hyprland-activation PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(lantern-hyprland-activation PUBLIC lantern-hyprland-configuration PRIVATE nlohmann_json::nlohmann_json lantern-hyprland-desktop lantern-hyprland-syntax)
