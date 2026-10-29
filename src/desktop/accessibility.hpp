// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "configuration/file-change.hpp"
#include "configuration/main-config.hpp"
#include "desktop/color.hpp"
#include "desktop/gsettings.hpp"
#include "desktop/terminal-theme.hpp"
#include "foundation/environment.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace desktop {

    // How the desktop is made easier to see and use. Each part goes where the desktop's
    // programs look for it: dconf's settings, which GTK apps and the desktop portal read,
    // Hyprland's config, and hyprtoolkit's.
    struct Accessibility {
        // GTK's high-contrast setting, and every terminal theme.
        Contrast contrast     = Contrast::standard;
        // Every terminal theme; and, with `wholeDesktop`, everything on the screen, through
        // a Hyprland screen shader.
        Vision   vision       = Vision::typical;
        bool     wholeDesktop = false;
        // Text in GTK apps and in hyprtoolkit's, 1 for as designed.
        double   textScale    = 1.0;
        // In pixels, for Hyprland's own cursor and the apps'.
        int      cursorSize   = 24;
        // No sliding or fading: Hyprland's animations off, and apps asked to keep still.
        bool     reduceMotion = false;

        friend bool operator==(const Accessibility&, const Accessibility&) = default;
    };

    // The words Lantern's settings keep for each choice, and back.
    [[nodiscard]] std::string_view           nameOf(Contrast contrast);
    [[nodiscard]] std::string_view           nameOf(Vision vision);
    [[nodiscard]] std::optional<Contrast>    contrastNamed(std::string_view name);
    [[nodiscard]] std::optional<Vision>      visionNamed(std::string_view name);

    // A Hyprland screen shader that corrects colors for a kind of color blindness: what
    // the eye cannot tell apart is moved to colors it can (daltonization). Nothing for
    // typical vision.
    [[nodiscard]] std::string           colorVisionShader(Vision vision);
    // What the shader makes of one color, for drawing it: the same sums, done here.
    [[nodiscard]] Color                corrected(const Color& color, Vision vision);
    // Where Lantern keeps it: $XDG_CONFIG_HOME/hypr/shaders, or ~/.config/hypr/shaders.
    [[nodiscard]] std::filesystem::path colorVisionShaderFile(Vision vision, const foundation::Environment& environment);

    // hyprtoolkit.conf's text sizes, its defaults scaled.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> toolkitTextSizes(double scale);

    // The desktop as it is now. Contrast is GTK's setting and the color vision is Lantern's
    // shader, if Hyprland uses one; Lantern keeps its own record of both too,
    // for the terminal, which the caller lays over these.
    [[nodiscard]] Accessibility currentAccessibility(const configuration::MainConfig& config, const foundation::Environment& environment);

    struct AccessibilityPlan {
        std::vector<configuration::FileChange> files;
        std::vector<GSetting>                  settings;

        [[nodiscard]] bool empty() const { return files.empty() && settings.empty(); }
    };

    // What gives the desktop `wanted`: the files that change and the settings that differ.
    // Hyprland's part needs a Lua config, which is followed when it is a link.
    [[nodiscard]] std::expected<AccessibilityPlan, std::string> planAccessibility(const Accessibility& wanted, const configuration::MainConfig& config,
                                                                                  const foundation::Environment& environment);

}
