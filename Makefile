# SPDX-License-Identifier: GPL-3.0-or-later
#
# A short way to build and install with CMake:
#
#   make                  build into build/release
#   make test             build and run the tests
#   sudo make install     install under /usr/local (PREFIX=... for elsewhere)
#   sudo make uninstall   remove exactly what make install put there
#   make clean            remove build/release
#
#   make HYPRTOOLKIT_PREFIX=/where/it/is/installed
#                         build against a hyprtoolkit installed there, one with its scroll
#                         area fixed, and use the scroll area for smooth scrolling
#
# Packagers can call CMake directly instead; this file adds nothing of its own.

PREFIX     ?= /usr/local
BUILD_TYPE ?= RelWithDebInfo
BUILD      ?= build/release
GENERATOR  ?= $(if $(shell command -v ninja),Ninja,Unix Makefiles)
HYPRTOOLKIT_PREFIX ?=
TOOLKIT    := $(if $(HYPRTOOLKIT_PREFIX),-DCMAKE_PREFIX_PATH=$(HYPRTOOLKIT_PREFIX) -DLANTERN_HYPRLAND_FIXED_HYPRTOOLKIT=ON,-DLANTERN_HYPRLAND_FIXED_HYPRTOOLKIT=OFF)

.PHONY: all configure test install uninstall clean

all: configure
	cmake --build $(BUILD)

configure:
	cmake -S . -B $(BUILD) -G "$(GENERATOR)" -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_INSTALL_PREFIX=$(PREFIX) $(TOOLKIT)

test: all
	ctest --test-dir $(BUILD) --output-on-failure

# Installs what make built, and builds nothing: run as root, building would leave files
# in the source tree that only root can remove.
install:
	@test -f $(BUILD)/src/lantern-hyprland || { echo "Run make first, as yourself."; exit 1; }
	cmake --install $(BUILD) --prefix $(PREFIX)

uninstall:
	@test -f $(BUILD)/install_manifest.txt || { echo "Nothing installed from $(BUILD)."; exit 1; }
	xargs --no-run-if-empty rm -f -- < $(BUILD)/install_manifest.txt

clean:
	rm -rf $(BUILD)
