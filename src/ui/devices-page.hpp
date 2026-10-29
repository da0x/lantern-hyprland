// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "desktop/devices.hpp"

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    // Sound, Bluetooth and the network: whether what runs them is running, what they are
    // doing, and buttons that open the tools made for them, with how to install a missing
    // one. Lantern changes nothing here; the tools do.
    class DevicesPage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        DevicesPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Asks again, when the page is opened.
        void refresh();

        // Draws the page again, after Lantern's colors or text size changed.
        void restyle();

      private:
        void show();
        void fillSound(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column);
        void fillBluetooth(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column);
        void fillNetwork(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column);
        // A button that opens `program`, when it is installed.
        void addOpen(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column, const std::string& label, std::string_view program);

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        application::EventLoop                                               loop;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> body;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    footer;

        // What the commands said, once they answered.
        std::optional<desktop::Volume>                                       volume;
        std::string                                                          output;
        std::optional<bool>                                                  bluetoothOn;
        std::optional<bool>                                                  networkOn;
        std::vector<desktop::Connection>                                     connections;
        std::string                                                          outcome;
    };

}
