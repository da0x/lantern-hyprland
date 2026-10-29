// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/session.hpp"

#include "application/loaded-config.hpp"
#include "ipc/request.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace application {

    namespace {

        // How long to wait before connecting again after the event socket drops.
        constexpr std::chrono::seconds retryDelay{2};

        // The events after which the monitors, workspaces or windows may differ.
        constexpr std::array<std::string_view, 25> treeEvents{
            "openwindow",    "closewindow",       "movewindowv2",     "workspacev2",      "focusedmonv2",  "activewindowv2", "createworkspacev2",
            "destroyworkspacev2", "moveworkspacev2", "renameworkspace", "monitoraddedv2", "monitorremovedv2", "changefloatingmode", "fullscreen",
            "windowtitlev2", "activespecialv2", "pin", "minimized", "configreloaded", "movewindow", "workspace", "focusedmon", "activewindow",
            "monitoradded", "monitorremoved",
        };

    }

    Session::Session(EventLoop loop, Listeners listeners) : loop{std::move(loop)}, listeners{std::move(listeners)} {}

    Session::~Session() {
        if (events)
            loop.unwatch(events->descriptor());
        loop.unwatch(worker.descriptor());
    }

    void Session::start() {
        loop.watch(worker.descriptor(), [this] { worker.runFinished(); });

        auto found = ipc::currentInstance();
        if (!found) {
            listeners.connection(Connection::lost, found.error());
            return;
        }
        instance = std::move(*found);
        connect();
    }

    void Session::ask(std::string typed, std::function<void(const std::expected<std::string, std::string>&)> answered) {
        if (!instance) {
            answered(std::unexpected{std::string{"not connected to Hyprland"}});
            return;
        }
        worker.post([instance = *instance, typed = std::move(typed), answered = std::move(answered)]() mutable -> Worker::Continuation {
            auto reply = ipc::requestTyped(instance, typed);
            return [answered = std::move(answered), reply = std::move(reply)] { answered(reply); };
        });
    }

    void Session::connect() {
        listeners.connection(Connection::connecting, {});

        auto opened = ipc::EventConnection::open(*instance);
        if (!opened) {
            lose(opened.error());
            return;
        }
        events = std::move(*opened);
        loop.watch(events->descriptor(), [this] { readEvents(); });

        // Subscribed first, then the snapshot, so no change can fall between the two.
        listeners.connection(Connection::live, {});
        refreshKeybindings();
        refreshOptions();
        refreshTree();
    }

    void Session::lose(const std::string& reason) {
        if (events) {
            loop.unwatch(events->descriptor());
            events.reset();
        }
        listeners.connection(Connection::lost, reason);
        loop.after(retryDelay, [this] { connect(); });
    }

    void Session::readEvents() {
        auto drained = events->drain();
        if (!drained) {
            lose(drained.error());
            return;
        }

        if (listeners.events && !drained->empty())
            listeners.events(*drained);

        // A reload replaces every binding and option, and no event reports a single one
        // changing. Hyprland also reloads when a config file changes, so this re-reads the
        // config too. An option changed with hyprctl keyword or eval reports nothing.
        if (std::ranges::any_of(*drained, [](const ipc::Event& event) { return event.name == "configreloaded"; })) {
            refreshKeybindings();
            refreshOptions();
        }
        // Monitors, workspaces and windows. A window dragged or resized by hand reports
        // nothing, so the tree shows where it was until the next event.
        if (std::ranges::any_of(*drained, [](const ipc::Event& event) { return std::ranges::contains(treeEvents, event.name); }))
            refreshTree();
    }

    void Session::refreshKeybindings() {
        worker.post([this, instance = *instance]() -> Worker::Continuation {
            const auto reply = ipc::request(instance, ipc::Query::bindings);
            auto       live  = reply ? ipc::parseBindings(*reply) : std::expected<std::vector<ipc::Binding>, std::string>{std::unexpect, reply.error()};

            std::expected<Keybindings, std::string> keybindings{std::unexpect, live ? std::string{} : live.error()};
            if (live) {
                const auto loaded    = findLoadedConfig(instance);
                const auto recording = loaded.config ? configuration::record(loaded.config->file) : std::expected<configuration::Recording, std::string>{std::unexpect, loaded.config.error()};
                keybindings          = joinKeybindings(std::move(*live), recording);
                keybindings->configurationDoubts = loaded.doubts;

                // The main file even when reading the config failed, since that is when
                // seeing it helps most.
                std::vector<std::filesystem::path> paths;
                if (loaded.config)
                    paths.push_back(std::filesystem::absolute(loaded.config->file).lexically_normal());
                if (recording)
                    for (const auto& path : recording->files)
                        if (!std::ranges::contains(paths, path))
                            paths.push_back(path);
                for (const auto& path : paths)
                    keybindings->sources.push_back(readSourceFile(path));
            }
            return [this, keybindings = std::move(keybindings)] { listeners.keybindings(keybindings); };
        });
    }

    void Session::refreshTree() {
        if (!listeners.tree)
            return;
        if (treeAsked) {
            treeAgain = true;
            return;
        }
        treeAsked = true;
        worker.post([this, instance = *instance]() -> Worker::Continuation {
            const auto monitors   = ipc::request(instance, ipc::Query::monitors);
            const auto workspaces = ipc::request(instance, ipc::Query::workspaces);
            const auto clients    = ipc::request(instance, ipc::Query::clients);
            std::expected<ipc::Tree, std::string> tree{std::unexpect, std::string{}};
            if (!monitors)
                tree = std::unexpected{monitors.error()};
            else if (!workspaces)
                tree = std::unexpected{workspaces.error()};
            else if (!clients)
                tree = std::unexpected{clients.error()};
            else
                tree = ipc::parseTree(*monitors, *workspaces, *clients);
            return [this, tree = std::move(tree)] {
                listeners.tree(tree);
                treeAsked = false;
                if (std::exchange(treeAgain, false))
                    refreshTree();
            };
        });
    }

    void Session::refreshOptions() {
        if (!listeners.options)
            return;
        worker.post([this, instance = *instance]() -> Worker::Continuation {
            const auto reply        = ipc::request(instance, ipc::Query::descriptions);
            auto       descriptions = reply ? ipc::parseOptionDescriptions(*reply) : std::expected<std::vector<ipc::OptionDescription>, std::string>{std::unexpect, reply.error()};
            std::expected<Options, std::string> options{std::unexpect, descriptions ? std::string{} : descriptions.error()};
            if (descriptions) {
                std::vector<std::string> names;
                for (const auto& description : *descriptions)
                    names.push_back(description.name);
                const auto values = ipc::requestOptions(instance, names);
                if (!values) {
                    options = std::unexpected{values.error()};
                } else {
                    const auto loaded    = findLoadedConfig(instance);
                    const auto recording = loaded.config ? configuration::record(loaded.config->file) : std::expected<configuration::Recording, std::string>{std::unexpect, loaded.config.error()};
                    options              = joinOptions(std::move(*descriptions), ipc::parseOptionValues(*values, names), recording);
                }
            }
            return [this, options = std::move(options)] { listeners.options(options); };
        });
    }

}
