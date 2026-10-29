// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/desktop-preview.hpp"

#include "ui/bar-preview.hpp"

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/palette/Gradient.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprutils::Memory::CSharedPointer;

        CSharedPointer<Hyprtoolkit::IElement> blank(double width, double height) {
            return Hyprtoolkit::CNullBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {width, height}})->commence();
        }

        CSharedPointer<Hyprtoolkit::IElement> stroke(std::string_view hex, double share, double height, double rounding) {
            return Hyprtoolkit::CRectangleBuilder::begin()
                ->color([c = paletteColor(hex)] { return c; })
                ->rounding(static_cast<int>(rounding))
                ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {share, height}})
                ->commence();
        }

        // A window as the theme draws it: the palette's colors inside, Hyprland's border
        // around, a gradient when it has the focus.
        CSharedPointer<Hyprtoolkit::CRectangleElement> window(const desktop::Palette& shown, bool focused, CDynamicSize size, double radius, int border, double scale) {
            auto builder = Hyprtoolkit::CRectangleBuilder::begin()
                               ->color([c = paletteColor(shown.base)] { return c; })
                               ->rounding(static_cast<int>(std::lround(radius)))
                               ->size(std::move(size));
            if (border > 0) {
                builder->borderThickness(border);
                if (focused)
                    builder->borderGradient([accent = paletteColor(shown.accent, 0.93F), audio = paletteColor(shown.audio, 0.93F)] {
                        Hyprtoolkit::CGradientValueData gradient;
                        gradient.m_vColors = {accent, audio};
                        gradient.m_fAngle  = std::numbers::pi_v<float> / 4;
                        return gradient;
                    });
                else
                    builder->borderColor([c = paletteColor(shown.raised, 0.67F)] { return c; });
            }
            const auto made = builder->commence();

            const auto inside = Hyprtoolkit::CColumnLayoutBuilder::begin()
                                    ->gap(static_cast<std::size_t>(8 * scale))
                                    ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})
                                    ->commence();
            inside->setMargin(static_cast<int>(std::max(14 * scale, radius / 2 + border)));
            if (focused) {
                // An app: a title, a panel with lines, and the accent button.
                inside->addChild(stroke(shown.text, 0.45, 10 * scale, 3 * scale));
                const auto panel = Hyprtoolkit::CRectangleBuilder::begin()
                                       ->color([c = paletteColor(shown.surface)] { return c; })
                                       ->rounding(static_cast<int>(6 * scale))
                                       ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 56 * scale}})
                                       ->commence();
                const auto lines = Hyprtoolkit::CColumnLayoutBuilder::begin()
                                       ->gap(static_cast<std::size_t>(8 * scale))
                                       ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})
                                       ->commence();
                lines->setMargin(static_cast<int>(10 * scale));
                lines->addChild(stroke(shown.subtle, 0.8, 6 * scale, 3 * scale));
                lines->addChild(stroke(shown.subtle, 0.6, 6 * scale, 3 * scale));
                lines->addChild(stroke(shown.audio, 0.4, 6 * scale, 3 * scale));
                panel->addChild(lines);
                inside->addChild(panel);
                inside->addChild(stroke(shown.accent, 0.3, 16 * scale, 5 * scale));
            } else {
                // A terminal: lines of output in a few colors.
                inside->addChild(stroke(shown.network, 0.5, 6 * scale, 3 * scale));
                inside->addChild(stroke(shown.subtle, 0.8, 6 * scale, 3 * scale));
                inside->addChild(stroke(shown.alert, 0.35, 6 * scale, 3 * scale));
            }
            made->addChild(inside);
            return made;
        }

    }

    CSharedPointer<Hyprtoolkit::IElement> desktopPreview(const desktop::Palette& palette, const desktop::WindowLook& look, const desktop::WaybarStyle& bar, double scale,
                                                         double height) {
        const auto radius  = look.rounding * scale;
        const auto border  = look.borderSize == 0 ? 0 : std::max(1, static_cast<int>(std::lround(look.borderSize * scale)));
        // Hyprland's inner gap is on each side of a window, so two are twice it apart.
        const auto between = static_cast<std::size_t>(std::lround(look.gapsIn * 2 * scale));
        const auto edge    = look.gapsOut * scale;

        const auto screen = Hyprtoolkit::CRectangleBuilder::begin()
                                ->color([c = paletteColor(palette.dark ? "#3b4252" : "#b6bccb")] { return c; })
                                ->rounding(static_cast<int>(8 * scale))
                                ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, height}})
                                ->commence();
        const auto inside = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        // The bar draws its own gap above and below; the windows are the outer gap below it.
        auto shownBar    = bar;
        shownBar.palette = &palette;
        inside->addChild(barPreview(shownBar, scale, false));
        inside->addChild(blank(1, std::max(0.0, edge - 10 * scale)));

        const auto line = Hyprtoolkit::CRowLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        line->setGrow(false, true);
        line->addChild(blank(edge, 1));
        const auto row = Hyprtoolkit::CRowLayoutBuilder::begin()->gap(between)->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        row->setGrow(true, false);
        // The focused window takes the width the stack on the right leaves.
        const auto focused = window(palette, true, CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}}, radius, border, scale);
        focused->setGrow(true, false);
        row->addChild(focused);
        const auto stack = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(between)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {0.42, 1.0}})->commence();
        const auto top   = window(palette, false, CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}}, radius, border, scale);
        top->setGrow(false, true);
        stack->addChild(top);
        stack->addChild(window(palette, false, CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 0.5}}, radius, border, scale));
        row->addChild(stack);
        line->addChild(row);
        line->addChild(blank(edge, 1));
        inside->addChild(line);
        inside->addChild(blank(1, edge));
        screen->addChild(inside);
        return screen;
    }

}
