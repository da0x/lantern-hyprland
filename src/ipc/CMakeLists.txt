# SPDX-License-Identifier: GPL-3.0-or-later
find_package(nlohmann_json 3.11 REQUIRED)

add_library(lantern-hyprland-ipc STATIC bindings.cpp events.cpp instance.cpp launch.cpp options.cpp request.cpp socket.cpp status.cpp tree.cpp)
target_include_directories(lantern-hyprland-ipc PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(lantern-hyprland-ipc PUBLIC lantern-hyprland-configuration lantern-hyprland-foundation PRIVATE nlohmann_json::nlohmann_json)
