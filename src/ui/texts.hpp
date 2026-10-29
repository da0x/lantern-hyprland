// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <string>

namespace ui {

    // Texts that span the width of their column and start at its left edge. A column
    // layout centers anything narrower than itself, so a text sized to its words would sit
    // in the middle.

    // A page's title.
    [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement> pageTitle(const std::string& text, const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>& palette);

    // Running text that wraps. `faded` is for what explains rather than what matters.
    [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement> paragraph(const std::string& text, const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>& palette,
                                                                                         bool faded = false);

}
