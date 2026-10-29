// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "desktop/accessibility.hpp"
#include "ui/choice-cards.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ui {

    // Contrast, color vision, text size, cursor size and motion, for the whole desktop:
    // dconf's settings, Hyprland's config, hyprtoolkit's, and every terminal theme. Written
    // only after the user reviews the change.
    class AccessibilityPage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        AccessibilityPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // What to do once settings were applied or undone, given what is now in use: show
        // the terminal's themes in that version, and Lantern's text at that size.
        void onApplied(std::function<void(const desktop::Accessibility&)> applied) { this->applied = std::move(applied); }

        // Draws the page again, after Lantern's colors changed.
        void restyle();

        // What is in use: the desktop's settings, with Lantern's own record of the
        // contrast and color vision for the terminal laid over them.
        [[nodiscard]] static desktop::Accessibility inUse();

      private:
        void showOverview();
        void showReview();
        void apply();
        void undo();
        void fillStatus();

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        application::EventLoop                                               loop;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> body;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    footer;
        ChoiceCards                                                          cards;
        RowList                                                              diff;


        desktop::Accessibility                                               wanted;
        desktop::Accessibility                                               current;
        bool                                                                 reviewing = false;
        desktop::AccessibilityPlan                                           plan;
        std::string                                                          outcome;
        std::string                                                          statusShown;
        std::function<void(const desktop::Accessibility&)>                   applied;
    };

}
