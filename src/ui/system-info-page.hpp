// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "configuration/file-change.hpp"
#include "desktop/fastfetch.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace ui {

    // fastfetch's summary of the system: its logo, its color from the terminal's scheme,
    // how much it shows, and whether each new terminal opens with it. Written only after the
    // user reviews the change.
    class SystemInfoPage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        SystemInfoPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Reads the files again, when the page is opened.
        void refresh();

        // Draws the page again, after Lantern's colors or text size changed.
        void restyle();

      private:
        void showOverview();
        // What fastfetch says with the settings chosen, drawn in the terminal's colors.
        void fillPreview();
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
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> preview;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    footer;
        RowList                                                              diff;

        // Nothing when the config is not one of Lantern's, or there is none.
        std::optional<desktop::Fastfetch>                                    now;
        desktop::Fastfetch                                                   wanted;
        bool                                                                 greetsNow    = false;
        bool                                                                 greetsWanted = false;
        // What fastfetch printed for the preview, and for which settings.
        std::vector<std::string>                                             lines;
        std::optional<desktop::Fastfetch>                                    linesFor;
        bool                                                                 reviewing = false;
        std::vector<configuration::FileChange>                               changes;
        std::string                                                          outcome;
    };

}
