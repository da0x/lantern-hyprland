# SPDX-License-Identifier: GPL-3.0-or-later
add_library(lantern-hyprland-foundation STATIC environment.cpp file-descriptor.cpp modifiers.cpp)
target_include_directories(lantern-hyprland-foundation PUBLIC ${PROJECT_SOURCE_DIR}/src)
