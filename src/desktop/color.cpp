// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/color.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>

namespace desktop {

    namespace {

        double linear(double channel) {
            return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
        }

        double gamma(double channel) {
            return channel <= 0.0031308 ? channel * 12.92 : 1.055 * std::pow(channel, 1.0 / 2.4) - 0.055;
        }

        double luminance(const Color& color) {
            return 0.2126 * linear(color.red) + 0.7152 * linear(color.green) + 0.0722 * linear(color.blue);
        }

        struct Lab {
            double lightness = 0;
            double a         = 0;
            double b         = 0;
        };

        // Björn Ottosson's OKLab, from linear sRGB and back.
        Lab labOf(const Color& color) {
            const auto red = linear(color.red), green = linear(color.green), blue = linear(color.blue);
            const auto l = std::cbrt(0.4122214708 * red + 0.5363325363 * green + 0.0514459929 * blue);
            const auto m = std::cbrt(0.2119034982 * red + 0.6806995451 * green + 0.1073969566 * blue);
            const auto s = std::cbrt(0.0883024619 * red + 0.2817188376 * green + 0.6299787005 * blue);
            return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s, 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                    0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
        }

        // In linear sRGB, which may fall outside 0 to 1.
        Color linearOf(const Lab& lab) {
            const auto l = std::pow(lab.lightness + 0.3963377774 * lab.a + 0.2158037573 * lab.b, 3.0);
            const auto m = std::pow(lab.lightness - 0.1055613458 * lab.a - 0.0638541728 * lab.b, 3.0);
            const auto s = std::pow(lab.lightness - 0.0894841775 * lab.a - 1.2914855480 * lab.b, 3.0);
            return {4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s, -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                    -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s};
        }

        bool inside(const Color& color) {
            constexpr double slack = 1e-6;
            return color.red >= -slack && color.red <= 1 + slack && color.green >= -slack && color.green <= 1 + slack && color.blue >= -slack && color.blue <= 1 + slack;
        }

    }

    Color parseColor(std::string_view hex) {
        unsigned value = 0;
        if (hex.size() == 7 && hex.front() == '#')
            std::from_chars(hex.data() + 1, hex.data() + 7, value, 16);
        return {static_cast<double>((value >> 16U) & 0xFFU) / 255.0, static_cast<double>((value >> 8U) & 0xFFU) / 255.0, static_cast<double>(value & 0xFFU) / 255.0};
    }

    std::string hexOf(const Color& color) {
        const auto byte = [](double channel) { return static_cast<unsigned>(std::lround(std::clamp(channel, 0.0, 1.0) * 255.0)); };
        return std::format("#{:02x}{:02x}{:02x}", byte(color.red), byte(color.green), byte(color.blue));
    }

    double contrast(const Color& one, const Color& other) {
        const auto first = luminance(one), second = luminance(other);
        return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
    }

    Lch lchOf(const Color& color) {
        const auto lab = labOf(color);
        auto       hue = std::atan2(lab.b, lab.a) * 180.0 / std::numbers::pi;
        if (hue < 0)
            hue += 360.0;
        return {lab.lightness, std::hypot(lab.a, lab.b), hue};
    }

    Color colorOf(const Lch& lch) {
        const auto lightness = std::clamp(lch.lightness, 0.0, 1.0);
        const auto radians   = lch.hue * std::numbers::pi / 180.0;
        // Less chroma until the color fits in sRGB: halving the step finds the most that does.
        double chroma = lch.chroma, low = 0.0, high = lch.chroma;
        auto   linearColor = linearOf({lightness, chroma * std::cos(radians), chroma * std::sin(radians)});
        if (!inside(linearColor)) {
            for (int step = 0; step < 24; ++step) {
                chroma       = (low + high) / 2;
                linearColor = linearOf({lightness, chroma * std::cos(radians), chroma * std::sin(radians)});
                (inside(linearColor) ? low : high) = chroma;
            }
            linearColor = linearOf({lightness, low * std::cos(radians), low * std::sin(radians)});
        }
        return {gamma(std::clamp(linearColor.red, 0.0, 1.0)), gamma(std::clamp(linearColor.green, 0.0, 1.0)), gamma(std::clamp(linearColor.blue, 0.0, 1.0))};
    }

    Color withContrast(const Color& color, const Color& background, double ratio) {
        if (contrast(color, background) >= ratio)
            return color;
        const bool upward = luminance(background) < 0.18;
        auto       lch    = lchOf(color);
        // The least step of lightness that is enough, found by halving. Each candidate is
        // judged as it will be written, in whole bytes, so rounding cannot undo the ratio.
        const auto written = [](const Lch& value) { return parseColor(hexOf(colorOf(value))); };
        double     near = lch.lightness, far = upward ? 1.0 : 0.0;
        for (int step = 0; step < 30; ++step) {
            const auto middle = (near + far) / 2;
            lch.lightness     = middle;
            (contrast(written(lch), background) >= ratio ? far : near) = middle;
        }
        lch.lightness = far;
        return written(lch);
    }

}
