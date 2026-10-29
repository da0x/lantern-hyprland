// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "desktop/waybar-style.hpp"
#include "desktop/window-look.hpp"

#include <hyprtoolkit/element/Element.hpp>

namespace ui {

    // A drawing of the desktop as it would look: Waybar in `bar`'s style at the top, and three
    // windows tiled as Hyprland tiles them, one focused beside two stacked, with `look`'s
    // corners, gaps and border, in `palette`'s colors. The focused one holds an app and has
    // the gradient border the theme gives it; the others hold a terminal. `height` is the
    // drawing's, and `scale` how much smaller than the screen it is drawn.
    [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> desktopPreview(const desktop::Palette& palette, const desktop::WindowLook& look,
                                                                                          const desktop::WaybarStyle& bar, double scale, double height);

}
