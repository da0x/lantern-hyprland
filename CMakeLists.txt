# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.30)
project(lantern-hyprland VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(hyprtoolkit REQUIRED IMPORTED_TARGET hyprtoolkit>=0.6.0)

option(LANTERN_HYPRLAND_WARNINGS_AS_ERRORS "Turn compiler warnings into errors (for CI)" OFF)
option(LANTERN_HYPRLAND_FIXED_HYPRTOOLKIT "Built against a hyprtoolkit whose scroll area does not crash (newer than 0.6.0, or patched)" OFF)

add_compile_options(-Wall -Wextra -Wpedantic)
if(LANTERN_HYPRLAND_WARNINGS_AS_ERRORS)
    add_compile_options(-Werror)
endif()

add_subdirectory(src)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
