// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    // A scrolling list of rows of text in columns.
    //
    // It keeps only as many row elements as fit and scrolls by changing their text, so a
    // list of thousands costs the same as a screenful. It does not use hyprtoolkit's scroll
    // area: in 0.6.0 that schedules a layout pass while the window is walking its scheduled
    // passes (IToolkitWindow::onPreRender), which corrupts the walk and crashes. For the same
    // reason, rows are added and removed only from `defer`, never during a layout pass.
    class RowList {
      public:
        struct Column {
            // In pixels; zero takes the width the other columns leave.
            double width     = 0;
            bool   monospace = false;
            // Smaller and fainter, for what matters less than the rest of the row.
            bool   secondary = false;
            // The column's text is Pango markup, already escaped. Other columns are shown as written.
            bool   markup    = false;
            // Shown above the column; when no column has one, there is no heading row.
            std::string heading;
        };

        struct Row {
            std::vector<std::string> cells;
            // Drawn with the accent behind it.
            bool                     marked = false;
        };

        // `textColor` replaces the palette's text color, for a list drawn on a background of its own.
        RowList(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, std::vector<Column> columns,
                double rowHeight, std::function<Hyprtoolkit::CHyprColor()> textColor = {});

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        void setRows(std::vector<Row> rows);

        // Called with the index of a row when it is clicked.
        void setClicked(std::function<void(std::size_t)> clicked);

        // Scrolls so that the row sits a third of the way down, or as near as the list allows.
        void scrollTo(std::size_t row);

      private:
        struct Shown {
            Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRectangleElement>         background;
            std::vector<Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>> cells;
        };

        void                                                                 fit(double height);
        void                                                                 scrollBy(std::ptrdiff_t rows);
        void                                                                 fill();
        [[nodiscard]] std::size_t                                            lastFirst() const;
        Shown                                                                makeRow(std::size_t index);
        [[nodiscard]] Hyprtoolkit::CHyprColor                                rowColor(std::size_t index) const;

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        std::vector<Column>                                                  columns;
        double                                                               rowHeight;
        std::function<Hyprtoolkit::CHyprColor()>                             textColor;
        std::function<void(std::size_t)>                                     clicked;

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CNullElement>         viewport;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> column;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         position;

        std::vector<Shown>                                                   shown;
        std::vector<Row>                                                     rows;
        std::size_t                                                          first   = 0;
        std::size_t                                                          fitting = 0;
        // A scrollTo that came before the list knew how many rows fit.
        std::optional<std::size_t>                                           wanted;
        // The shown row under the pointer.
        std::optional<std::size_t>                                           hovered;
        bool                                                                 fitPending = false;
    };

}
