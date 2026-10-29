// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "configuration/file-change.hpp"
#include "desktop/gsettings.hpp"
#include "desktop/input.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Image.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/palette/Palette.hpp>
#include <hyprtoolkit/system/Icons.hpp>

#include <chrono>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    // The keyboard's layouts and the keys that switch them, what Caps Lock does, how held
    // keys repeat, the mouse's speed, buttons and double click, and how the touchpad clicks,
    // scrolls and swipes. Written to the Hyprland config, and the double click to GTK's
    // settings, only after the user reviews the change.
    class InputPage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        InputPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop,
                  Hyprutils::Memory::CSharedPointer<Hyprtoolkit::ISystemIconFactory> icons);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Draws the page again, after Lantern's colors or text size changed.
        void restyle();

      private:
        void showOverview();
        void showReview();
        void apply();
        void undo();
        void fillKeyboard(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column);
        void fillMouse(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column);
        void fillTouchpad(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column);
        // A dropdown of two choices for a setting that is on or off.
        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> choice(const std::string& name, const std::string& about, const std::string& off, const std::string& on,
                                                                                      bool desktop::InputSettings::*setting);
        // A folder that opens on a double click as quick as the one chosen, to try it on.
        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> doubleClickTest();
        void                                                                   showTestFolder();
        void fillStatus();
        // Fills the status once a slider has stopped for a moment: planning the change and
        // drawing the footer at every step it passes leaves no time to draw the slider.
        void settleStatus();
        [[nodiscard]] std::expected<std::optional<configuration::FileChange>, std::string> plan() const;
        // GTK's double-click time, when the one chosen is not the one set.
        [[nodiscard]] std::vector<desktop::GSetting> planGtk() const;
        void reread();

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        application::EventLoop                                               loop;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> body;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    footer;
        RowList                                                              diff;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::ISystemIconFactory>   icons;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CImageElement>        testFolder;
        std::chrono::steady_clock::time_point                                lastClick;
        bool                                                                 folderOpen  = false;
        unsigned                                                             statusAsked = 0;

        // The layouts xkb knows, by name.
        std::vector<desktop::KnownLayout>                                    known;
        // What the config sets now, and what is chosen; nothing when it cannot be read.
        std::optional<desktop::InputSettings>                                current;
        bool                                                                 touchpad = false;
        desktop::InputSettings                                               wanted;
        // GTK's double-click time in milliseconds, as set and as chosen; nothing set when
        // gsettings is not installed.
        std::optional<int>                                                   doubleClickNow;
        int                                                                  doubleClick = 400;
        bool                                                                 reviewing   = false;
        std::vector<configuration::FileChange>                               changes;
        std::vector<desktop::GSetting>                                       gtk;
        std::string                                                          outcome;
    };

}
