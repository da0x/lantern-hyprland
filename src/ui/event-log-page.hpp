// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "ipc/events.hpp"
#include "ipc/tree.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/element/Textbox.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ui {

    // What Hyprland reports as it happens, newest first, each event said in words. The
    // log keeps the last few thousand, from when Lantern started.
    class EventLogPage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        EventLogPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Without its title and margin, as a tab of a page that has them; see TabbedPage.
        void embed();

        void showConnection(application::Connection connection, const std::string& reason);
        void addEvents(const std::vector<ipc::Event>& events);
        // Learns the windows' names, so events that name a window by its address can say
        // which it is.
        void learnWindows(const ipc::Tree& tree);
        // Draws the list again, after Lantern's colors changed.
        void restyle();

      private:
        struct Entry {
            std::string time;
            ipc::Event  event;
            // Said when it came, while the window it names was still known.
            std::string described;
        };

        void updateStatus();
        void updateButtons();
        void showRows();

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>        palette;
        std::function<void(std::function<void()>)>                      defer;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         title;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>    status;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextboxElement> search;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement>  pauseButton;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement>  everyButton;
        RowList                                                         list;

        std::deque<Entry>                  entries;
        // Window names by address, "0x…".
        std::map<std::string, std::string> windows;
        std::string                        query;
        // Paused, the list stays as it is while events are still kept.
        bool                               paused = false;
        // Also the older form of events Hyprland sends twice.
        bool                               every  = false;
        // Kept while paused, shown on resuming.
        std::size_t                        unseen = 0;
        bool                               refreshPending = false;
        application::Connection            connection = application::Connection::connecting;
        std::string                        connectionReason;
    };

}
