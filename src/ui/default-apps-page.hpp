// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "configuration/file-change.hpp"
#include "desktop/default-apps.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    // The app that opens each kind of link and file, such as the web browser, chosen among
    // those installed that say they open it, and the terminal that apps which run in one
    // open in. Written only after the user reviews the change.
    class DefaultAppsPage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        DefaultAppsPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Reads the files again, when the page is opened.
        void refresh();

        // Draws the page again, after Lantern's colors or text size changed.
        void restyle();

      private:
        void showOverview();
        void addKind(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column, const desktop::DefaultKind& kind);
        void addTerminal(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column);
        void showReview();
        void apply();
        void undo();
        void fillStatus();
        [[nodiscard]] std::expected<std::vector<configuration::FileChange>, std::string> plan() const;

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        application::EventLoop                                               loop;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> body;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    footer;
        RowList                                                              diff;

        std::vector<desktop::Application>                                    installed;
        // By the kind's name, the application's id; none when nothing is chosen.
        std::map<std::string, std::optional<std::string>>                    now;
        std::map<std::string, std::optional<std::string>>                    wanted;
        // By the kind's name, what opens it while nothing is chosen.
        std::map<std::string, std::optional<std::string>>                    fallback;
        std::optional<std::string>                                           terminalNow;
        std::optional<std::string>                                           terminalWanted;
        bool                                                                 reviewing = false;
        std::vector<configuration::FileChange>                               changes;
        std::string                                                          outcome;
    };

}
