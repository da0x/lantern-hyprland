// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/source-file.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/palette/Palette.hpp>

#include <functional>

namespace ui {

    // A config file as it is on disk, with line numbers and Lua syntax colors.
    class SourceView {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        SourceView(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return lines.element(); }

        // Shows the file with `marked` highlighted, counting from 1; 0 marks nothing. With
        // `jump`, scrolls to the marked line, or else stays where it was.
        void show(const application::SourceFile& file, int marked, bool jump);

      private:
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette;
        RowList                                                  lines;
    };

}
