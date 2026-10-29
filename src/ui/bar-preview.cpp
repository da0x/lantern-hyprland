// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/bar-preview.hpp"

#include "ui/markup.hpp"

#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <string>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprtoolkit::CFontSize;
        using Hyprutils::Memory::CSharedPointer;

        // Waybar's sizes in Lantern's config, in pixels.
        constexpr double barHeight      = 38;
        constexpr double fullHeight     = 34;
        // The gap above the bar, kept as drawn whatever the windows' gap is, since the
        // previews' heights are laid out from it; the sides follow the windows.
        constexpr double topGap         = 10;
        constexpr double pillWidth      = 30;
        constexpr double pillGap        = 4;
        constexpr double groupPadding   = 6;
        constexpr double clockWidth     = 180;
        // Waybar's 15px font, in points.
        constexpr float  fontPoints     = 11.25F;
        constexpr float  islandOpacity  = 0.92F;

        Hyprtoolkit::CHyprColor color(std::string_view hexColor, float alpha = 1.F) {
            unsigned value = 0;
            if (hexColor.size() == 7)
                std::from_chars(hexColor.data() + 1, hexColor.data() + 7, value, 16);
            return {static_cast<float>((value >> 16U) & 0xFFU) / 255.F, static_cast<float>((value >> 8U) & 0xFFU) / 255.F, static_cast<float>(value & 0xFFU) / 255.F, alpha};
        }

        CSharedPointer<Hyprtoolkit::IElement> gap(double width, double height = 1) {
            return Hyprtoolkit::CNullBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {width, height}})->commence();
        }

        CSharedPointer<Hyprtoolkit::CRowLayoutElement> row(double width, double height, double spacing = 0);

        // Text centered in a box `width` wide.
        CSharedPointer<Hyprtoolkit::IElement> label(const std::string& markup, std::string_view hexColor, double width, double scale, double height = 0) {
            const auto text = Hyprtoolkit::CTextBuilder::begin()
                                  ->text(std::format("<span font_family=\"Adwaita Sans, Symbols Nerd Font\">{}</span>", markup))
                                  ->fontSize(CFontSize{CFontSize::HT_FONT_ABSOLUTE, fontPoints * static_cast<float>(scale)})
                                  ->color([c = color(hexColor)] { return c; })
                                  ->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})
                                  ->commence();
            // A column centers a narrower child across but starts it at the top, so a spacer
            // above takes half of what the line leaves. A column sized to its content
            // comes out empty in 0.6.0, so the box has a fixed height.
            const auto boxHeight  = height > 0 ? height : barHeight * scale;
            const auto lineHeight = fontPoints * scale * 4.0 / 3.0 * 1.2;
            const auto box        = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {width, boxHeight}})->commence();
            box->addChild(gap(1, std::max(0.0, (boxHeight - lineHeight) / 2)));
            box->addChild(text);
            return box;
        }

        // The left and right groups, each as wide as it needs, and the center between them. In
        // hyprtoolkit 0.6.0 only the first growing child of a row takes the space left over,
        // so two springs cannot center anything; a column centers a narrower child, so the
        // center is one, and the only one that grows.
        CSharedPointer<Hyprtoolkit::IElement> thirds(const CSharedPointer<Hyprtoolkit::IElement>& left, const CSharedPointer<Hyprtoolkit::IElement>& center,
                                                     const CSharedPointer<Hyprtoolkit::IElement>& right, double height, bool grow, double leftSide, double rightSide) {
            const auto whole = row(grow ? 1 : 0, height);
            if (grow)
                whole->setGrow(true, false);
            const auto fixed = [height](double side) { return CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {side, height}}; };
            const auto start = Hyprtoolkit::CRowLayoutBuilder::begin()->size(fixed(leftSide))->commence();
            start->addChild(left);
            const auto middle = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, height}})->commence();
            middle->setGrow(true, false);
            middle->addChild(center);
            const auto end = Hyprtoolkit::CRowLayoutBuilder::begin()->size(fixed(rightSide))->commence();
            end->addChild(right);
            whole->addChild(start);
            whole->addChild(middle);
            whole->addChild(end);
            return whole;
        }

        CSharedPointer<Hyprtoolkit::CRowLayoutElement> row(double width, double height, double spacing) {
            return Hyprtoolkit::CRowLayoutBuilder::begin()
                ->gap(static_cast<std::size_t>(spacing))
                ->size(CDynamicSize{width > 0 ? CDynamicSize::HT_SIZE_ABSOLUTE : CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {width > 0 ? width : 1.0, height}})
                ->commence();
        }

        // How wide each widget is drawn, before scaling: its sample text or icon and padding.
        // The tray is drawn empty, as it is until a program puts an icon in it.
        double widgetWidth(desktop::Widget widget) {
            switch (widget) {
                case desktop::Widget::workspaces: return 5 * pillWidth + 4 * pillGap + 2 * groupPadding;
                case desktop::Widget::windowTitle: return 70;
                case desktop::Widget::clock: return clockWidth;
                case desktop::Widget::weather: return 96;
                case desktop::Widget::media: return 134;
                case desktop::Widget::privacy:
                case desktop::Widget::keepAwake:
                case desktop::Widget::bluetooth: return 40;
                case desktop::Widget::cpu:
                case desktop::Widget::memory:
                case desktop::Widget::sound:
                case desktop::Widget::battery: return 80;
                case desktop::Widget::temperature: return 92;
                case desktop::Widget::network: return 90;
                case desktop::Widget::tray: return 2;
            }
            return 0;
        }

        // Workspace 1 active, 2 and 3 with windows, 4 and 5 empty.
        CSharedPointer<Hyprtoolkit::IElement> workspaces(const desktop::WaybarStyle& style, double scale, double height) {
            const auto& palette  = *style.palette;
            const auto  rounding = style.geometry.rounding;
            const auto  made     = row(widgetWidth(desktop::Widget::workspaces) * scale, height, pillGap * scale);
            made->addChild(gap((groupPadding - pillGap) * scale));
            for (int number = 1; number <= 5; ++number) {
                const auto textColor = number == 1 ? palette.onAccent : number <= 3 ? palette.subtle : palette.raised;
                const auto pill       = Hyprtoolkit::CRectangleBuilder::begin()
                                      ->color([c = color(palette.accent, number == 1 ? 1.F : 0.F)] { return c; })
                                      ->rounding(static_cast<int>(std::lround(rounding * 0.7 * scale)))
                                      ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {pillWidth * scale, height - 10 * scale}})
                                      ->commence();
                const auto inside = row(pillWidth * scale, height - 10 * scale);
                inside->addChild(label(std::to_string(number), textColor, pillWidth * scale, scale, height - 10 * scale));
                pill->addChild(inside);
                made->addChild(pill);
            }
            return made;
        }

        CSharedPointer<Hyprtoolkit::IElement> widget(const desktop::WaybarStyle& style, desktop::Widget widget, double scale, double height) {
            const auto& palette = *style.palette;
            const auto  width   = widgetWidth(widget) * scale;
            switch (widget) {
                case desktop::Widget::workspaces: return workspaces(style, scale, height);
                case desktop::Widget::windowTitle: return label("kitty", palette.subtle, width, scale, height);
                case desktop::Widget::clock: return label("<b>Tue 20 Oct   14:30</b>", palette.text, width, scale, height);
                case desktop::Widget::weather: return label("\U000F0595  +18°C", palette.text, width, scale, height);
                case desktop::Widget::media: return label("\U000F075A  Lofi radio", palette.text, width, scale, height);
                case desktop::Widget::privacy: return label("\U000F0567", palette.alert, width, scale, height);
                case desktop::Widget::cpu: return label("\U000F061A  12%", palette.subtle, width, scale, height);
                case desktop::Widget::memory: return label("\U000F035B  38%", palette.subtle, width, scale, height);
                case desktop::Widget::temperature: return label("\U000F050F  52°C", palette.subtle, width, scale, height);
                case desktop::Widget::keepAwake: return label("\U000F0209", palette.muted, width, scale, height);
                case desktop::Widget::bluetooth: return label("\U000F00AF", palette.audio, width, scale, height);
                case desktop::Widget::battery: return label("\U000F0080  80%", palette.text, width, scale, height);
                case desktop::Widget::sound: return label("\U000F057E  40%", palette.audio, width, scale, height);
                case desktop::Widget::network: return label("\U000F0928  home", palette.network, width, scale, height);
                case desktop::Widget::tray: return gap(width);
            }
            return gap(width);
        }

        // The widgets on one side, in a row as wide as they are.
        double sideWidth(const desktop::WaybarStyle& style, desktop::Side side, double scale) {
            double width = 0;
            for (const auto shown : style.widgets[static_cast<std::size_t>(side)])
                width += widgetWidth(shown);
            return width * scale;
        }

        CSharedPointer<Hyprtoolkit::IElement> group(const desktop::WaybarStyle& style, desktop::Side side, double scale, double height) {
            const auto made = row(std::max(1.0, sideWidth(style, side, scale)), height);
            for (const auto shown : style.widgets[static_cast<std::size_t>(side)])
                made->addChild(widget(style, shown, scale, height));
            return made;
        }

        // How a style's border is drawn at `scale`: its color, and at least a pixel wide.
        Hyprtoolkit::CHyprColor borderColor(const desktop::WaybarStyle& style) {
            switch (style.border) {
                case desktop::Border::thin: return color(style.palette->surface);
                case desktop::Border::inactive: return color(style.palette->raised, 0.67F);
                case desktop::Border::active: return color(style.palette->accent, 0.93F);
            }
            return {};
        }

        int borderWidth(const desktop::WaybarStyle& style, double scale) {
            return std::max(1, static_cast<int>(std::lround((style.border == desktop::Border::thin ? 1 : style.geometry.border) * scale)));
        }

        // A bar or an island, with `bordered`'s border when there is one.
        CSharedPointer<Hyprtoolkit::CRectangleElement> panel(const desktop::Palette& palette, double width, double height, double rounding, float alpha,
                                                             const desktop::WaybarStyle* bordered, double scale) {
            auto builder = Hyprtoolkit::CRectangleBuilder::begin()
                               ->color([c = color(palette.base, alpha)] { return c; })
                               ->rounding(static_cast<int>(rounding))
                               ->size(CDynamicSize{width > 0 ? CDynamicSize::HT_SIZE_ABSOLUTE : CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {width > 0 ? width : 1.0, height}});
            if (bordered)
                builder->borderColor([c = borderColor(*bordered)] { return c; })->borderThickness(borderWidth(*bordered, scale));
            return builder->commence();
        }

        CSharedPointer<Hyprtoolkit::IElement> island(const CSharedPointer<Hyprtoolkit::IElement>& content, double width, const desktop::Palette& palette, double scale, double rounding,
                                                     const desktop::WaybarStyle& style) {
            const auto made = panel(palette, width, barHeight * scale, rounding * scale, islandOpacity, &style, scale);
            made->addChild(content);
            return made;
        }

    }

    Hyprtoolkit::CHyprColor paletteColor(std::string_view hex, float alpha) {
        return color(hex, alpha);
    }

    CSharedPointer<Hyprtoolkit::IElement> widgetPreview(const desktop::WaybarStyle& style, desktop::Widget shown, double scale) {
        const auto height = barHeight * scale;
        // The tray draws nothing until a program puts an icon there, so it shows one.
        const auto width = (shown == desktop::Widget::tray ? 40.0 : widgetWidth(shown)) * scale;
        const auto piece = Hyprtoolkit::CRectangleBuilder::begin()
                               ->color([c = color(style.palette->base, islandOpacity)] { return c; })
                               ->rounding(static_cast<int>(std::lround(std::min(style.geometry.rounding * scale, height / 2))))
                               ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {width, height}})
                               ->commence();
        const auto inside = row(width, height);
        if (shown == desktop::Widget::tray) {
            const auto column = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {width, height}})->commence();
            column->addChild(gap(1, (height - 16 * scale) / 2));
            column->addChild(Hyprtoolkit::CRectangleBuilder::begin()
                                 ->color([c = color(style.palette->subtle)] { return c; })
                                 ->rounding(static_cast<int>(4 * scale))
                                 ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {16 * scale, 16 * scale}})
                                 ->commence());
            inside->addChild(column);
        } else {
            inside->addChild(widget(style, shown, scale, height));
        }
        piece->addChild(inside);
        return piece;
    }

    double barPreviewHeight(const desktop::WaybarStyle& style, double scale) {
        return style.shape == desktop::Shape::fullWidth ? (fullHeight + 2 * topGap) * scale : (barHeight + 2 * topGap) * scale;
    }

    CSharedPointer<Hyprtoolkit::IElement> barPreview(const desktop::WaybarStyle& style, double scale, bool backdropShown) {
        const auto& palette  = *style.palette;
        const bool  floating = style.shape != desktop::Shape::fullWidth;
        const auto  height   = barPreviewHeight(style, scale);

        // The desktop behind the bar: a soft tone that shows a light or a dark bar.
        const auto backdrop = Hyprtoolkit::CRectangleBuilder::begin()
                                  ->color([c = palette.dark ? color("#4b5263", backdropShown ? 1.F : 0.F) : color("#9aa3b5", backdropShown ? 1.F : 0.F)] { return c; })
                                  ->rounding(static_cast<int>(8 * scale))
                                  ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, height}})
                                  ->commence();
        const auto stack = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        backdrop->addChild(stack);
        if (floating)
            stack->addChild(gap(1, topGap * scale));

        const auto line    = row(0, (floating ? barHeight : fullHeight) * scale);
        const auto bar     = line;
        const auto barSize = (floating ? barHeight : fullHeight) * scale;
        if (floating)
            line->addChild(gap(style.geometry.edgeGap * scale));

        const auto left  = std::max(1.0, sideWidth(style, desktop::Side::left, scale));
        const auto right = std::max(1.0, sideWidth(style, desktop::Side::right, scale));
        switch (style.shape) {
            case desktop::Shape::islands: {
                // A side with nothing on it has no island.
                const auto islandOf = [&](desktop::Side side) -> CSharedPointer<Hyprtoolkit::IElement> {
                    const auto width = sideWidth(style, side, scale);
                    if (width <= 0)
                        return gap(1, barSize);
                    return island(group(style, side, scale, barSize), width, palette, scale, style.geometry.rounding, style);
                };
                line->addChild(thirds(islandOf(desktop::Side::left), islandOf(desktop::Side::center), islandOf(desktop::Side::right), barSize, true, left, right));
                break;
            }
            case desktop::Shape::floating: {
                const auto whole = panel(palette, 1, barSize, style.geometry.rounding * scale, islandOpacity, &style, scale);
                whole->setGrow(true, false);
                whole->addChild(thirds(group(style, desktop::Side::left, scale, barSize), group(style, desktop::Side::center, scale, barSize), group(style, desktop::Side::right, scale, barSize), barSize, false, left, right));
                line->addChild(whole);
                break;
            }
            case desktop::Shape::fullWidth: {
                // The border runs only along the bottom, which a rectangle's own cannot draw.
                const double underline = borderWidth(style, scale);
                const double above     = barSize - underline;
                const auto   column    = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, barSize}})->commence();
                column->setGrow(true, false);
                const auto whole = panel(palette, 0, above, 0, 1.F, nullptr, scale);
                whole->addChild(thirds(group(style, desktop::Side::left, scale, above), group(style, desktop::Side::center, scale, above), group(style, desktop::Side::right, scale, above), above, false, left, right));
                column->addChild(whole);
                column->addChild(Hyprtoolkit::CRectangleBuilder::begin()
                                     ->color([c = borderColor(style)] { return c; })
                                     ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, underline}})
                                     ->commence());
                line->addChild(column);
                break;
            }
        }
        if (floating)
            line->addChild(gap(style.geometry.edgeGap * scale));
        stack->addChild(bar);
        return backdrop;
    }

}
