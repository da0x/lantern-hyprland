// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/tabbed-page.hpp"

#include "ui/texts.hpp"
#include "ui/widgets.hpp"

#include <algorithm>

namespace ui {

    using Hyprtoolkit::CDynamicSize;

    TabbedPage::TabbedPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, const std::string& title,
                           std::vector<Tab> tabs) :
        palette{std::move(palette)}, defer{std::move(defer)}, tabs{std::move(tabs)} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        root->addChild(pageTitle(title, this->palette));
        const auto row = leftRow();
        row->rebuild()->gap(4)->commence();
        for (std::size_t index = 0; index < this->tabs.size(); ++index) {
            auto made = button(this->tabs[index].label, 130, index == 0, [this, index] { this->defer([this, index] { showTab(index); }); });
            row->addChild(made);
            buttons.push_back(std::move(made));
        }
        root->addChild(row);
        // As tall as what is left of the page, which the tab's page fills.
        holder = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        holder->setGrow(false, true);
        root->addChild(holder);
        holder->addChild(this->tabs.front().page);
    }

    bool TabbedPage::select(std::string_view name) {
        const auto found = std::ranges::find_if(tabs, [name](const Tab& tab) { return tab.label == name || (!tab.formerly.empty() && tab.formerly == name); });
        if (found == tabs.end())
            return false;
        showTab(static_cast<std::size_t>(found - tabs.begin()));
        return true;
    }

    void TabbedPage::showTab(std::size_t index) {
        if (index >= tabs.size() || index == shown)
            return;
        buttons[shown]->rebuild()->accent(false)->commence();
        buttons[index]->rebuild()->accent(true)->commence();
        shown = index;
        holder->clearChildren();
        holder->addChild(tabs[index].page);
        if (switched)
            switched();
    }

}
