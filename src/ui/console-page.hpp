// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/element/Textbox.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <array>
#include <expected>
#include <functional>
#include <string>

namespace ui {

    // Asks Hyprland what it knows and shows the reply as it came. It sends only requests
    // that read: what may be asked is the list beside the reply.
    class ConsolePage {
      public:
        // The colors the reply is drawn in, kitty's when its theme is one Lantern knows.
        struct Terminal {
            std::string                 background;
            std::string                 foreground;
            std::array<std::string, 16> colors;
        };

        using Ask = std::function<void(std::string typed, std::function<void(const std::expected<std::string, std::string>&)> answered)>;

        // `defer` runs a function after the current layout pass; see RowList.
        ConsolePage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Without its title and margin, as a tab of a page that has them; see TabbedPage.
        void embed();

        // How requests reach Hyprland, once the session is there.
        void onAsk(Ask ask);
        // Draws the lists again, after Lantern's colors changed.
        void restyle();

      private:
        void readTerminal();
        void send(std::string typed);
        void showRequests();
        void showReply();

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        Ask                                                                  ask;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         title;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextboxElement>      typed;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         status;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRectangleElement>    screen;
        Terminal                                                             terminal;
        RowList                                                              requests;
        RowList                                                              reply;

        // What was last sent, and what came back or why it was not sent.
        std::string                                                          asked;
        std::expected<std::string, std::string>                              answer{std::unexpect, std::string{}};
        // Replies that come after a newer request was sent are dropped.
        unsigned                                                             sent = 0;
    };

}
