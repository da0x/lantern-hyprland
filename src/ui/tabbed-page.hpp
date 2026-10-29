// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

    // A page made of other pages, one shown at a time under a row of tabs: pages that
    // belong together without fitting on one, such as the displays and the night light.
    // Each page is given without its own title; see the pages' embed().
    class TabbedPage {
      public:
        struct Tab {
            std::string                                              label;
            Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> page;
            // What the page was called when it had a sidebar entry of its own, so that
            // --page still opens it.
            std::string                                              formerly;
        };

        // `defer` runs a function after the current layout pass; see RowList.
        TabbedPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, const std::string& title,
                   std::vector<Tab> tabs);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // The page of the tab shown.
        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> current() const { return tabs[shown].page; }

        // Shows the tab with this label or former name, if there is one.
        bool select(std::string_view name);

        // Told after another tab is shown.
        void onSwitched(std::function<void()> switched) { this->switched = std::move(switched); }

      private:
        void showTab(std::size_t index);

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>                    palette;
        std::function<void(std::function<void()>)>                                  defer;
        std::vector<Tab>                                                            tabs;
        std::vector<Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement>> buttons;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>        root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>        holder;
        std::size_t                                                                 shown = 0;
        std::function<void()>                                                       switched;
    };

}
