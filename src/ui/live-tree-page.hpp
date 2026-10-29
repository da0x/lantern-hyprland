// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "ipc/tree.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    // Hyprland's monitors, workspaces and windows as they are now, kept current by the
    // session: a map of the monitors with the windows on them where they are, and the same
    // as a tree. Clicking a window in the tree picks it out on the map.
    class LiveTreePage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        LiveTreePage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Without its title and margin, as a tab of a page that has them; see TabbedPage.
        void embed();

        void showConnection(application::Connection connection, const std::string& reason);
        void showTree(const std::expected<ipc::Tree, std::string>& tree);
        // Draws the map and the tree again, after Lantern's colors changed.
        void restyle();

      private:
        void updateStatus();
        void showMap();
        void showRows();

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         title;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         status;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> map;
        RowList                                                              list;

        ipc::Tree                  tree;
        // The window each row of the list is, when it is one.
        std::vector<std::string>   rowWindows;
        std::optional<std::string> selected;
        application::Connection    connection = application::Connection::connecting;
        std::string                connectionReason;
        std::string                treeError;
        bool                       received = false;
    };

}
