# SPDX-License-Identifier: GPL-3.0-or-later
pkg_check_modules(lua REQUIRED IMPORTED_TARGET lua5.5)

add_library(lantern-hyprland-lua STATIC state.cpp value.cpp)
target_include_directories(lantern-hyprland-lua PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(lantern-hyprland-lua PRIVATE PkgConfig::lua)
