# SPDX-License-Identifier: GPL-3.0-or-later
enable_language(C)
pkg_check_modules(paper REQUIRED IMPORTED_TARGET wayland-client hyprgraphics cairo)
pkg_get_variable(waylandProtocols wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(layerShell ${PROJECT_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml)
set(generated ${CMAKE_CURRENT_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${generated})
add_custom_command(
    OUTPUT ${generated}/wlr-layer-shell-unstable-v1-client-protocol.h ${generated}/wlr-layer-shell-unstable-v1-protocol.c
    COMMAND ${WAYLAND_SCANNER} client-header ${layerShell} ${generated}/wlr-layer-shell-unstable-v1-client-protocol.h
    # The protocol names an argument namespace, a word C++ keeps for itself.
    COMMAND ${CMAKE_COMMAND} -DHEADER=${generated}/wlr-layer-shell-unstable-v1-client-protocol.h -P ${CMAKE_CURRENT_SOURCE_DIR}/rename-namespace.cmake
    COMMAND ${WAYLAND_SCANNER} private-code ${layerShell} ${generated}/wlr-layer-shell-unstable-v1-protocol.c
    DEPENDS ${layerShell} ${CMAKE_CURRENT_SOURCE_DIR}/rename-namespace.cmake)

# The layer shell's popups are xdg-shell's.
set(xdgShell ${waylandProtocols}/stable/xdg-shell/xdg-shell.xml)
add_custom_command(
    OUTPUT ${generated}/xdg-shell-protocol.c
    COMMAND ${WAYLAND_SCANNER} private-code ${xdgShell} ${generated}/xdg-shell-protocol.c
    DEPENDS ${xdgShell})

add_executable(lantern-paper main.cpp ${generated}/wlr-layer-shell-unstable-v1-protocol.c ${generated}/xdg-shell-protocol.c)
target_include_directories(lantern-paper PRIVATE ${generated})
target_link_libraries(lantern-paper PRIVATE lantern-hyprland-desktop lantern-hyprland-ipc PkgConfig::paper)
install(TARGETS lantern-paper RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
