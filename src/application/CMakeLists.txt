# SPDX-License-Identifier: GPL-3.0-or-later
find_package(nlohmann_json 3.11 REQUIRED)
pkg_check_modules(libsystemd REQUIRED IMPORTED_TARGET libsystemd)

add_library(lantern-hyprland-application STATIC keybindings.cpp command.cpp config-check.cpp event-description.cpp file-chooser.cpp loaded-config.cpp options.cpp outside-program.cpp session.cpp settings.cpp lantern-folder.cpp source-file.cpp worker.cpp)
target_include_directories(lantern-hyprland-application PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(lantern-hyprland-application PUBLIC lantern-hyprland-configuration lantern-hyprland-desktop lantern-hyprland-ipc lantern-hyprland-syntax PRIVATE nlohmann_json::nlohmann_json PkgConfig::libsystemd)
