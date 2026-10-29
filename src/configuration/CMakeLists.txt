# SPDX-License-Identifier: GPL-3.0-or-later
add_library(lantern-hyprland-configuration STATIC autostart.cpp bindings.cpp diff.cpp file-change.cpp hyprlang-edit.cpp location.cpp lua-binds.cpp lua-edit.cpp main-config.cpp recording.cpp value.cpp)
target_include_directories(lantern-hyprland-configuration PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(lantern-hyprland-configuration PUBLIC lantern-hyprland-foundation PRIVATE lantern-hyprland-lua lantern-hyprland-syntax)
# recording.cpp embeds recorder.lua; rebuild it when the script changes.
set_property(SOURCE recording.cpp APPEND PROPERTY OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/recorder.lua)
