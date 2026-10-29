// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "desktop/waybar-style.hpp"

#include <hyprtoolkit/element/Element.hpp>
#include <hyprtoolkit/palette/Color.hpp>

#include <string_view>

namespace ui {

    // A drawing of Waybar in a style, over a stand-in for the desktop behind it: the
    // workspaces, the clock, audio, network and the tray, in the style's colors and shape.
    // At `scale` 1 it is the size Waybar draws itself. Without `backdrop` the desktop
    // behind is left to whatever the bar is drawn on.
    [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> barPreview(const desktop::WaybarStyle& style, double scale, bool backdrop = true);

    // One widget by itself, as `style` draws it on the bar, on a piece of the bar behind it.
    [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> widgetPreview(const desktop::WaybarStyle& style, desktop::Widget widget, double scale);

    // A palette's #rrggbb as a color.
    [[nodiscard]] Hyprtoolkit::CHyprColor paletteColor(std::string_view hex, float alpha = 1.F);

    // Its height at `scale`.
    [[nodiscard]] double barPreviewHeight(const desktop::WaybarStyle& style, double scale);

}
