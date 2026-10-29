// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "configuration/file-change.hpp"
#include "desktop/cursor-theme.hpp"
#include "desktop/gsettings.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    // The mouse cursor's theme: those installed, drawn from their own files, and those the
    // distribution offers, with the command that installs one. Written only after the user
    // reviews the change.
    class CursorsPage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        CursorsPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Draws the page again, after Lantern's colors or text size changed.
        void restyle();

      private:
        void showOverview();
        void showReview();
        void apply();
        void undo();
        void updateSelection();
        void fillStatus();
        // The command that installs the package of the theme picked, when it is not installed.
        void showInstallHint();
        [[nodiscard]] std::expected<std::vector<configuration::FileChange>, std::string> plan() const;

        struct Card {
            std::string                                                       name;
            Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRectangleElement> frame;
            Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>      title;
            std::string                                                       label;
        };

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        application::EventLoop                                               loop;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> body;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> hint;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    footer;
        RowList                                                              diff;
        std::vector<desktop::CursorTheme>                                    themes;
        std::vector<Card>                                                    cards;
        // The theme in use, the one picked, and the one shown with how to install it.
        std::string                                                          inUse;
        std::string                                                          selected;
        std::string                                                          installing;
        bool                                                                 reviewing = false;
        std::vector<configuration::FileChange>                               changes;
        std::vector<desktop::GSetting>                                       gtk;
        std::string                                                          outcome;
        std::string                                                          statusShown;
    };

}
