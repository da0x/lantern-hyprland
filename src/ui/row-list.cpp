// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/row-list.hpp"

#include "ui/markup.hpp"
#include "ui/restyle.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprtoolkit::CFontSize;

        constexpr double rowGap           = 2;
        constexpr int    rowsPerWheelStep = 3;
        constexpr int    columnGap        = 12;
        // Space before the first column, inside the row's background.
        constexpr double padding          = 20;

        // Asks the color function again. hyprtoolkit offers no other way than rebuilding,
        // which schedules a layout pass, so never call this from inside one.
        void recolor(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRectangleElement>& rectangle) {
            rectangle->rebuild()->commence();
        }

    }

    RowList::RowList(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, std::vector<Column> columns,
                     double rowHeight, std::function<Hyprtoolkit::CHyprColor()> textColor) :
        palette{std::move(palette)}, defer{std::move(defer)}, columns{std::move(columns)}, rowHeight{rowHeight}, textColor{std::move(textColor)} {
        const auto text = [this] { return this->textColor ? this->textColor() : this->palette->m_colors.text; };

        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(4)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        root->setGrow(false, true);

        // Takes the height left over; its height decides how many rows fit.
        viewport = Hyprtoolkit::CNullBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        viewport->setGrow(false, true);
        viewport->setReceivesMouse(true);
        viewport->setMouseAxis([this](Hyprtoolkit::Input::eAxisAxis axis, float delta) {
            if (axis == Hyprtoolkit::Input::AXIS_AXIS_VERTICAL && delta != 0.F)
                scrollBy(delta > 0.F ? rowsPerWheelStep : -rowsPerWheelStep);
        });
        // Called during a layout pass, so the rows change afterwards.
        viewport->setRepositioned([this] {
            const double height = viewport->size().y;
            if (fitPending)
                return;
            fitPending = true;
            this->defer([this, height] {
                fitPending = false;
                fit(height);
            });
        });

        column = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(static_cast<std::size_t>(rowGap))->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        viewport->addChild(column);

        // Headings over the columns, padded like a row so they line up.
        if (std::ranges::any_of(this->columns, [](const Column& spec) { return !spec.heading.empty(); })) {
            const auto headings = Hyprtoolkit::CRowLayoutBuilder::begin()->gap(columnGap)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 22.0}})->commence();
            headings->addChild(Hyprtoolkit::CNullBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {padding - columnGap, 1.0}})->commence());
            for (const auto& spec : this->columns) {
                auto heading = Hyprtoolkit::CTextBuilder::begin()
                                   ->text(escapeMarkup(spec.heading.empty() ? std::string{" "} : spec.heading))
                                   ->fontSize(CFontSize{CFontSize::HT_FONT_SMALL})
                                   ->color(text)
                                   ->a(0.5F)
                                   ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {spec.width > 0 ? spec.width : 1.0, 1.0}})
                                   ->commence();
                if (spec.width <= 0)
                    heading->setGrow(true, false);
                restyled(heading, CFontSize{CFontSize::HT_FONT_SMALL});
                headings->addChild(heading);
            }
            root->addChild(headings);
            const auto separator = Hyprtoolkit::CRectangleBuilder::begin()
                                       ->color([p = this->palette] { return p->m_colors.text.mix(p->m_colors.base, 0.85F); })
                                       ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})
                                       ->commence();
            restyled(separator);
            root->addChild(separator);
        }

        position = Hyprtoolkit::CTextBuilder::begin()->text(" ")->fontSize(CFontSize{CFontSize::HT_FONT_SMALL})->color(text)->a(0.6F)->commence();
        restyled(position, CFontSize{CFontSize::HT_FONT_SMALL});

        root->addChild(viewport);
        root->addChild(position);
    }

    void RowList::setRows(std::vector<Row> newRows) {
        rows  = std::move(newRows);
        first = std::min(first, lastFirst());
        fill();
    }

    void RowList::setClicked(std::function<void(std::size_t)> function) {
        clicked = std::move(function);
    }

    void RowList::scrollTo(std::size_t row) {
        if (fitting == 0) {
            wanted = row;
            return;
        }
        first = std::min(row > fitting / 3 ? row - fitting / 3 : 0, lastFirst());
        fill();
    }

    std::size_t RowList::lastFirst() const {
        return rows.size() > fitting ? rows.size() - fitting : 0;
    }

    Hyprtoolkit::CHyprColor RowList::rowColor(std::size_t index) const {
        const auto& colors = palette->m_colors;
        const auto  at     = first + index;
        if (index < fitting && at < rows.size() && rows[at].marked)
            return Hyprtoolkit::CHyprColor{colors.accent.asRGB(), 0.22F};
        if (clicked && hovered == index && at < rows.size())
            return Hyprtoolkit::CHyprColor{colors.text.asRGB(), 0.07F};
        return Hyprtoolkit::CHyprColor{colors.base.asRGB(), 0.F};
    }

    RowList::Shown RowList::makeRow(std::size_t index) {
        const auto text = [this] { return textColor ? textColor() : palette->m_colors.text; };
        Shown      made;
        made.background = Hyprtoolkit::CRectangleBuilder::begin()
                              ->color([this, index] { return rowColor(index); })
                              ->rounding(palette->m_vars.smallRounding)
                              ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, rowHeight}})
                              ->commence();
        const auto row = Hyprtoolkit::CRowLayoutBuilder::begin()->gap(columnGap)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        made.background->addChild(row);
        restyled(made.background);

        // The row takes the pointer, so the wheel has to scroll from here too.
        made.background->setReceivesMouse(true);
        made.background->setMouseAxis([this](Hyprtoolkit::Input::eAxisAxis axis, float delta) {
            if (axis == Hyprtoolkit::Input::AXIS_AXIS_VERTICAL && delta != 0.F)
                scrollBy(delta > 0.F ? rowsPerWheelStep : -rowsPerWheelStep);
        });
        made.background->setMouseEnter([this, index](const Hyprutils::Math::Vector2D&) {
            hovered = index;
            recolor(shown[index].background);
        });
        made.background->setMouseLeave([this, index] {
            if (hovered == index)
                hovered.reset();
            recolor(shown[index].background);
        });
        made.background->setMouseButton([this, index](Hyprtoolkit::Input::eMouseButton button, bool pressed) {
            const auto at = first + index;
            if (button != Hyprtoolkit::Input::MOUSE_BUTTON_LEFT || !pressed || !clicked || at >= rows.size())
                return;
            // What a click does may change the page this row is on, so not from inside its handler.
            defer([this, at] {
                if (clicked)
                    clicked(at);
            });
        });

        row->addChild(Hyprtoolkit::CNullBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {padding - columnGap, 1.0}})->commence());
        for (const auto& spec : columns) {
            // Sized to its text in height, so the row centers it.
            auto builder = Hyprtoolkit::CTextBuilder::begin()->text(" ")->color(text)->size(
                CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {spec.width > 0 ? spec.width : 1.0, 1.0}});
            if (spec.monospace)
                builder->fontFamily(std::string{palette->m_vars.fontFamilyMonospace});
            if (spec.secondary)
                builder->fontSize(CFontSize{CFontSize::HT_FONT_SMALL})->a(0.6F);
            auto cell = builder->commence();
            if (spec.width <= 0)
                cell->setGrow(true, false);
            restyled(cell, CFontSize{spec.secondary ? CFontSize::HT_FONT_SMALL : CFontSize::HT_FONT_TEXT});
            row->addChild(cell);
            made.cells.push_back(std::move(cell));
        }
        return made;
    }

    void RowList::fit(double height) {
        fitting = static_cast<std::size_t>(std::max(0.0, std::floor((height + rowGap) / (rowHeight + rowGap))));

        // Row elements are made once and kept; ones that do not fit are only detached.
        while (shown.size() < fitting)
            shown.push_back(makeRow(shown.size()));
        for (std::size_t index = 0; index < shown.size(); ++index) {
            column->removeChild(shown[index].background);
            if (index < fitting)
                column->addChild(shown[index].background);
        }

        if (wanted && fitting != 0) {
            const auto row = *wanted;
            wanted.reset();
            scrollTo(row);
            return;
        }
        first = std::min(first, lastFirst());
        fill();
    }

    void RowList::scrollBy(std::ptrdiff_t delta) {
        const auto moved = std::clamp(static_cast<std::ptrdiff_t>(first) + delta, std::ptrdiff_t{0}, static_cast<std::ptrdiff_t>(lastFirst()));
        if (static_cast<std::size_t>(moved) == first)
            return;
        first = static_cast<std::size_t>(moved);
        fill();
    }

    void RowList::fill() {
        for (std::size_t index = 0; index < std::min(fitting, shown.size()); ++index) {
            const auto  at  = first + index;
            const auto* row = at < rows.size() ? &rows[at] : nullptr;
            for (std::size_t cell = 0; cell < columns.size(); ++cell) {
                const std::string_view value = row != nullptr && cell < row->cells.size() ? std::string_view{row->cells[cell]} : std::string_view{};
                // An empty text is laid out as nothing, so blank cells hold a space.
                shown[index].cells[cell]->setText(value.empty() ? std::string{" "} : columns[cell].markup ? std::string{value} : escapeMarkup(value));
            }
            recolor(shown[index].background);
        }

        if (rows.size() <= fitting || fitting == 0)
            position->setText(" ");
        else
            position->setText(std::format("Rows {} to {} of {}. Scroll for more.", first + 1, std::min(first + fitting, rows.size()), rows.size()));
    }

}
