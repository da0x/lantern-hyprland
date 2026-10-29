// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "configuration/file-change.hpp"
#include "desktop/idle.hpp"
#include "desktop/waybar.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    // What happens while the computer is left alone (dimming, a screensaver, locking, turning
    // the screens off and suspending, through hypridle), a lock screen in the theme's colors
    // through hyprlock with its background, clock and date chosen, a key that locks at once,
    // and the power mode and brightness. The files are written only after the user reviews
    // the change; the power mode and brightness change at once.
    class IdlePage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        IdlePage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Reads the files and asks for the power mode and brightness again, when the page is
        // opened.
        void refresh();

        // Draws the page again, after Lantern's colors or text size changed.
        void restyle();

      private:
        void showOverview();
        void showReview();
        void apply();
        void undo();
        void fillIdle(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column,
                      const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& side);
        void fillLock(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column,
                      const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& side);
        void fillPower(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column,
                      const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& side);
        // A box for something missing, whose Check again reads the page again.
        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> requirement(const desktop::Requirement& needed);
        // The lock screen as designed, small, in the theme's colors.
        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> lockPreview() const;
        void fillStatus();
        // Starts hypridle again, so that it reads its new config.
        void restartIdle();
        [[nodiscard]] std::expected<std::vector<configuration::FileChange>, std::string> plan() const;

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        application::EventLoop                                               loop;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> body;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    footer;
        RowList                                                              diff;

        desktop::IdleSettings                                                current;
        desktop::IdleSettings                                                wanted;
        // What hyprlock.conf is now: missing, Lantern's, or the user's own.
        enum class LockLook { none, lantern, yours };
        LockLook                                                             lockNow    = LockLook::none;
        bool                                                                 themedLock = false;
        // Lantern's lock screen as it is drawn now, when it is Lantern's, and as chosen.
        std::optional<desktop::LockDesign>                                   designNow;
        desktop::LockDesign                                                  design;
        // The keys that lock at once, as the Hyprland config binds them and as chosen;
        // empty for none.
        std::string                                                          lockKeyNow;
        std::string                                                          lockKey;
        // The Hyprland config's text, to tell what a chosen key does now.
        std::string                                                          hyprlandText;
        std::optional<std::filesystem::path>                                 wallpaperImage;
        // The power mode and brightness as last read; empty when they cannot be.
        std::vector<std::string>                                             powerModes;
        std::string                                                          powerMode;
        std::optional<int>                                                   brightness;
        // The tab shown; the settings and what is missing are side by side on each.
        enum class Tab { idle, lock, power };
        Tab                                                                  tab       = Tab::idle;
        bool                                                                 reviewing = false;
        std::vector<configuration::FileChange>                               changes;
        std::string                                                          outcome;
    };

}
