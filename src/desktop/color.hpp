// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace desktop {

    // A color in sRGB, each channel from 0 to 1.
    struct Color {
        double red   = 0;
        double green = 0;
        double blue  = 0;

        bool operator==(const Color&) const = default;
    };

    // #rrggbb, either case; anything else is black.
    [[nodiscard]] Color      parseColor(std::string_view hex);
    [[nodiscard]] std::string hexOf(const Color& color);

    // WCAG's contrast ratio of two colors, from 1 to 21.
    [[nodiscard]] double contrast(const Color& one, const Color& other);

    // The color in OKLCH: lightness 0 to 1, chroma, and hue in degrees. A perceptual space,
    // so changing the lightness leaves the hue and the colorfulness as they look.
    struct Lch {
        double lightness = 0;
        double chroma    = 0;
        double hue       = 0;
    };
    [[nodiscard]] Lch    lchOf(const Color& color);
    // Back to sRGB; a color outside it loses chroma until it fits.
    [[nodiscard]] Color colorOf(const Lch& lch);

    // The color with the least change of lightness, toward white on a dark background and
    // toward black on a light one, that has at least `ratio` contrast with `background`.
    [[nodiscard]] Color withContrast(const Color& color, const Color& background, double ratio);

}
