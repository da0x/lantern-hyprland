// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/devices-page.hpp"

#include "application/command.hpp"
#include "desktop/clipboard.hpp"
#include "desktop/process.hpp"
#include "desktop/system.hpp"
#include "desktop/waybar.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <format>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprutils::Memory::CSharedPointer;

        std::optional<std::filesystem::path> program(std::string_view name) {
            return desktop::findProgram(name, foundation::variable(foundation::currentEnvironment(), "PATH").value_or(""));
        }

        bool running(std::string_view name) {
            return !desktop::findProcesses(name).empty();
        }

    }

    DevicesPage::DevicesPage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop) :
        palette{std::move(palette)}, defer{std::move(defer)}, loop{std::move(loop)} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        root->addChild(pageTitle("Sound, Bluetooth and network", this->palette));
        root->addChild(paragraph("What runs each of them, what it is doing, and the tool made for changing it. Lantern opens the tool rather than copying it.", this->palette));
        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        root->addChild(body);
        footer = leftRow();
        root->addChild(footer);
        refresh();
    }

    void DevicesPage::refresh() {
        show();
        const auto ask = [this](std::string_view name, std::vector<std::string> arguments, std::function<void(bool, std::string)> answered) {
            if (const auto found = program(name))
                (void)application::runCommand(loop, *found, std::move(arguments), [this, answered = std::move(answered)](bool succeeded, std::string text) {
                    answered(succeeded, std::move(text));
                    show();
                });
        };
        ask("wpctl", {"get-volume", "@DEFAULT_AUDIO_SINK@"}, [this](bool succeeded, std::string text) { volume = succeeded ? desktop::parseVolume(text) : std::nullopt; });
        ask("wpctl", {"inspect", "@DEFAULT_AUDIO_SINK@"}, [this](bool succeeded, std::string text) { output = succeeded ? desktop::parseNodeName(text) : ""; });
        // is-active prints "active" and succeeds only for a running service.
        ask("systemctl", {"is-active", "bluetooth.service"}, [this](bool succeeded, std::string) { bluetoothOn = succeeded; });
        ask("systemctl", {"is-active", "NetworkManager.service"}, [this](bool succeeded, std::string) { networkOn = succeeded; });
        ask("nmcli", {"-t", "-f", "NAME,TYPE,DEVICE", "connection", "show", "--active"},
            [this](bool succeeded, std::string text) { connections = succeeded ? desktop::parseConnections(text) : std::vector<desktop::Connection>{}; });
    }

    void DevicesPage::restyle() {
        show();
    }

    void DevicesPage::show() {
        body->clearChildren();
        body->addChild(space(4));
        // Sound and the network on the left, Bluetooth on the right, each column as tall as
        // the page so that both start at the top.
        const auto columns = leftRow(0);
        columns->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->gap(32)->commence();
        const auto left  = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(8)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {0.48, 1.0}})->commence();
        const auto right = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(8)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {0.48, 1.0}})->commence();
        columns->addChild(left);
        columns->addChild(right);
        body->addChild(columns);
        fillSound(left);
        left->addChild(space(scaled(12.0)));
        fillNetwork(left);
        fillBluetooth(right);
        setFooter(footer, {},
                  outcome.empty() ? nullptr : CSharedPointer<Hyprtoolkit::IElement>{markupLine(escapeMarkup(outcome), palette)});
    }

    void DevicesPage::addOpen(const CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column, const std::string& label, std::string_view name) {
        const auto found = program(name);
        if (!found)
            return;
        const auto row = leftRow(scaled(40.0));
        row->addChild(button(label, 260, false, [this, found = *found, label] {
            defer([this, found, label] {
                const auto started = desktop::startDetached(found);
                outcome            = started ? std::string{} : std::format("{} did not open: {}.", label, started.error());
                show();
            });
        }));
        column->addChild(row);
    }

    void DevicesPage::fillSound(const CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column) {
        column->addChild(heading("Sound", palette));
        if (!running("pipewire") || !running("wireplumber")) {
            column->addChild(paragraph("PipeWire is not running, so there is no sound. It starts with your login when pipewire, pipewire-pulse and wireplumber are "
                                       "installed.",
                                       palette));
            return;
        }
        column->addChild(paragraph(running("pipewire-pulse") ? "PipeWire is running, for every app." : "PipeWire is running, but apps made for PulseAudio cannot use it: "
                                                                                                        "install pipewire-pulse.",
                                   palette));
        if (volume)
            column->addChild(paragraph(volume->muted ? std::format("{} is muted.", output.empty() ? "The output" : output)
                                                     : std::format("Playing through {} at {}%.", output.empty() ? "the default output" : output, volume->percent),
                                       palette));
        addOpen(column, "Open the volume control", "pavucontrol");
        addOpen(column, "Open Hyprland's sound center", "hyprpwcenter");
        if (!program("pavucontrol") && !program("hyprpwcenter"))
            column->addChild(requirementBlock(desktop::Requirement{.name     = "Volume control",
                                                                   .purpose  = "Picks the output and the microphone, and sets each app's volume.",
                                                                   .upstream = "https://freedesktop.org/software/pulseaudio/pavucontrol/",
                                                                   .package  = "pavucontrol",
                                                                   .found    = std::nullopt},
                                              desktop::currentDistribution(), desktop::clipboardProgram(foundation::variable(foundation::currentEnvironment(), "PATH").value_or("")),
                                              palette, defer, [this] { refresh(); }));
    }

    void DevicesPage::fillBluetooth(const CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column) {
        column->addChild(heading("Bluetooth", palette));
        const auto adapters = desktop::bluetoothAdapters();
        if (adapters.empty()) {
            column->addChild(paragraph("This computer has no Bluetooth adapter, or its driver is not loaded.", palette));
            return;
        }
        const auto copier = desktop::clipboardProgram(foundation::variable(foundation::currentEnvironment(), "PATH").value_or(""));
        if (!program("bluetoothctl")) {
            column->addChild(paragraph(std::format("The adapter, {}, is there, but nothing runs it.", adapters.front()), palette));
            column->addChild(requirementBlock(desktop::Requirement{.name     = "Bluetooth service",
                                                                   .purpose  = "BlueZ, which connects headphones, keyboards and other Bluetooth devices.",
                                                                   .upstream = "https://www.bluez.org",
                                                                   .package  = "bluez bluez-utils",
                                                                   .found    = std::nullopt},
                                              desktop::currentDistribution(), copier, palette, defer, [this] { refresh(); }));
            return;
        }
        if (bluetoothOn == false) {
            column->addChild(paragraph("BlueZ is installed, but its service is not running. Run this in a terminal to start it now and with every boot, then "
                                       "press Check again:",
                                       palette));
            column->addChild(commandRow("sudo systemctl enable --now bluetooth.service", copier, palette, defer));
            const auto again = leftRow(34);
            again->addChild(button("Check again", 140, false, [this] { defer([this] { refresh(); }); }));
            column->addChild(again);
            return;
        }
        if (bluetoothOn)
            column->addChild(paragraph(std::format("Bluetooth is on, through {}.", adapters.front()), palette));
        addOpen(column, "Open the Bluetooth manager", "blueman-manager");
        if (!program("blueman-manager"))
            column->addChild(requirementBlock(desktop::Requirement{.name     = "Bluetooth manager",
                                                                   .purpose  = "Finds devices, pairs them and connects them, with a tray icon.",
                                                                   .upstream = "https://github.com/blueman-project/blueman",
                                                                   .package  = "blueman",
                                                                   .found    = std::nullopt},
                                              desktop::currentDistribution(), copier, palette, defer, [this] { refresh(); }));
    }

    void DevicesPage::fillNetwork(const CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column) {
        column->addChild(heading("Network", palette));
        if (networkOn == false) {
            column->addChild(paragraph("NetworkManager is not running. The tools below need it; another service, such as iwd or systemd-networkd, may be "
                                       "connecting this computer instead.",
                                       palette));
            return;
        }
        if (networkOn) {
            if (connections.empty())
                column->addChild(paragraph("Not connected.", palette));
            for (const auto& connection : connections)
                column->addChild(paragraph(std::format("Connected to {} ({}, {}).", connection.name, connection.kind, connection.device), palette));
        }
        addOpen(column, "Open the network connections", "nm-connection-editor");
        if (!program("nm-connection-editor"))
            column->addChild(requirementBlock(desktop::Requirement{.name     = "Network connections",
                                                                   .purpose  = "Sets up Wi-Fi, wired, VPN and other connections for NetworkManager.",
                                                                   .upstream = "https://gitlab.gnome.org/GNOME/network-manager-applet",
                                                                   .package  = "nm-connection-editor",
                                                                   .found    = std::nullopt},
                                              desktop::currentDistribution(), desktop::clipboardProgram(foundation::variable(foundation::currentEnvironment(), "PATH").value_or("")),
                                              palette, defer, [this] { refresh(); }));
    }

}
