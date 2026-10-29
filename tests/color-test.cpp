// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/color.hpp"

#include <gtest/gtest.h>

namespace {

    using desktop::parseColor;

    TEST(Color, ReadsAndWritesHex) {
        EXPECT_EQ(desktop::hexOf(parseColor("#1E1E2E")), "#1e1e2e");
        EXPECT_EQ(desktop::hexOf(parseColor("nonsense")), "#000000");
    }

    TEST(Color, MeasuresContrastAsWcagDoes) {
        EXPECT_NEAR(desktop::contrast(parseColor("#000000"), parseColor("#ffffff")), 21.0, 1e-9);
        EXPECT_NEAR(desktop::contrast(parseColor("#777777"), parseColor("#ffffff")), 4.48, 0.01);
    }

    TEST(Color, GoesThroughOklchAndBack) {
        for (const auto* hex : {"#f38ba8", "#a6e3a1", "#1e66f5", "#000000", "#ffffff", "#808080"}) {
            const auto color = parseColor(hex);
            EXPECT_EQ(desktop::hexOf(desktop::colorOf(desktop::lchOf(color))), hex);
        }
        // Red is near hue 25, blue near 265.
        EXPECT_NEAR(desktop::lchOf(parseColor("#ff0000")).hue, 29.2, 0.5);
        EXPECT_NEAR(desktop::lchOf(parseColor("#0000ff")).hue, 264.1, 0.5);
    }

    TEST(Color, ReachesAContrastKeepingTheHue) {
        const auto background = parseColor("#282a36");
        const auto dim        = parseColor("#6272a4");
        const auto raised     = desktop::withContrast(dim, background, 7.0);
        EXPECT_GE(desktop::contrast(raised, background), 7.0);
        EXPECT_LT(desktop::contrast(raised, background), 7.3);
        EXPECT_NEAR(desktop::lchOf(raised).hue, desktop::lchOf(dim).hue, 3.0);

        // On a light background, darker.
        const auto light = parseColor("#fbf1c7");
        const auto dark  = desktop::withContrast(parseColor("#d79921"), light, 7.0);
        EXPECT_GE(desktop::contrast(dark, light), 7.0);
        EXPECT_LT(desktop::lchOf(dark).lightness, desktop::lchOf(parseColor("#d79921")).lightness);
    }

}
