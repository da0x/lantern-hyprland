// SPDX-License-Identifier: GPL-3.0-or-later
#include "foundation/modifiers.hpp"

#include <gtest/gtest.h>

using foundation::Modifiers;

TEST(Modifiers, WritesThemInTheOrderPeopleSayThem) {
    // SHIFT (1) + CTRL (4) + ALT (8) + SUPER (64)
    EXPECT_EQ(Modifiers{77}.with("T"), "SUPER + CTRL + ALT + SHIFT + T");
}

TEST(Modifiers, WritesTheKeyAloneWithoutModifiers) {
    EXPECT_EQ(Modifiers{}.with("Print"), "Print");
    EXPECT_TRUE(Modifiers{}.empty());
}

TEST(Modifiers, NamesTheRarelyUsedOnes) {
    EXPECT_EQ(Modifiers{2 | 16 | 32 | 128}.with("A"), "CAPS + MOD2 + MOD3 + MOD5 + A");
}
