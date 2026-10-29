// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <hyprtoolkit/element/Textbox.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

#include <string>
#include <vector>

namespace ui {

    // hyprtoolkit gives every key to the window before the text box that has the keyboard,
    // and does not say which box that is. So a key that is a shortcut is told from one
    // typed into a box by what the boxes hold before and after it. Every text box is
    // registered here.
    void registerTextbox(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextboxElement>& box);

    // What each registered text box holds now.
    [[nodiscard]] std::vector<std::string> textboxTexts();

}
