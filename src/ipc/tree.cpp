// SPDX-License-Identifier: GPL-3.0-or-later
#include "ipc/tree.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace ipc {

    namespace {

        // A field of the wrong type reads as missing, as in parseBindings.
        std::string text(const nlohmann::json& object, const char* name) {
            const auto field = object.find(name);
            return field != object.end() && field->is_string() ? field->get<std::string>() : std::string{};
        }

        double number(const nlohmann::json& object, const char* name) {
            const auto field = object.find(name);
            return field != object.end() && field->is_number() ? field->get<double>() : 0.0;
        }

        int whole(const nlohmann::json& object, const char* name) {
            return static_cast<int>(std::lround(number(object, name)));
        }

        bool flag(const nlohmann::json& object, const char* name) {
            const auto field = object.find(name);
            return field != object.end() && field->is_boolean() && field->get<bool>();
        }

        // {"id": 1, "name": "1"} under `name`.
        int workspaceOf(const nlohmann::json& object, const char* name) {
            const auto field = object.find(name);
            return field != object.end() && field->is_object() ? whole(*field, "id") : 0;
        }

        // The pair under `name`: [x, y].
        std::pair<int, int> pairOf(const nlohmann::json& object, const char* name) {
            const auto field = object.find(name);
            if (field == object.end() || !field->is_array() || field->size() != 2 || !(*field)[0].is_number() || !(*field)[1].is_number())
                return {0, 0};
            return {static_cast<int>(std::lround((*field)[0].get<double>())), static_cast<int>(std::lround((*field)[1].get<double>()))};
        }

        std::expected<nlohmann::json, std::string> list(std::string_view reply, std::string_view what) {
            auto document = nlohmann::json::parse(reply, nullptr, false);
            if (document.is_discarded() || !document.is_array())
                return std::unexpected{std::string{"the "} + std::string{what} + " reply is not a JSON list"};
            return document;
        }

    }

    std::expected<Tree, std::string> parseTree(std::string_view monitors, std::string_view workspaces, std::string_view clients) {
        const auto monitorList   = list(monitors, "monitors");
        const auto workspaceList = list(workspaces, "workspaces");
        const auto clientList    = list(clients, "clients");
        if (!monitorList)
            return std::unexpected{monitorList.error()};
        if (!workspaceList)
            return std::unexpected{workspaceList.error()};
        if (!clientList)
            return std::unexpected{clientList.error()};

        Tree tree;
        for (const auto& object : *monitorList)
            if (object.is_object())
                tree.monitors.push_back(Monitor{
                    .id               = whole(object, "id"),
                    .name             = text(object, "name"),
                    .description      = text(object, "description"),
                    .x                = whole(object, "x"),
                    .y                = whole(object, "y"),
                    .width            = whole(object, "width"),
                    .height           = whole(object, "height"),
                    .refreshRate      = number(object, "refreshRate"),
                    .scale            = object.contains("scale") ? number(object, "scale") : 1.0,
                    .focused          = flag(object, "focused"),
                    .disabled         = flag(object, "disabled"),
                    .activeWorkspace  = workspaceOf(object, "activeWorkspace"),
                    .specialWorkspace = workspaceOf(object, "specialWorkspace"),
                });
        for (const auto& object : *workspaceList)
            if (object.is_object())
                tree.workspaces.push_back(Workspace{
                    .id            = whole(object, "id"),
                    .name          = text(object, "name"),
                    .monitor       = text(object, "monitor"),
                    .windows       = whole(object, "windows"),
                    .hasFullscreen = flag(object, "hasfullscreen"),
                });
        for (const auto& object : *clientList) {
            if (!object.is_object())
                continue;
            const auto [x, y]          = pairOf(object, "at");
            const auto [width, height] = pairOf(object, "size");
            tree.windows.push_back(Window{
                .address     = text(object, "address"),
                .windowClass = text(object, "class"),
                .title       = text(object, "title"),
                .workspace   = workspaceOf(object, "workspace"),
                .monitor     = whole(object, "monitor"),
                .x           = x,
                .y           = y,
                .width       = width,
                .height      = height,
                .floating    = flag(object, "floating"),
                .fullscreen  = whole(object, "fullscreen") != 0,
                .pinned      = flag(object, "pinned"),
                .xwayland    = flag(object, "xwayland"),
                .hidden      = flag(object, "hidden"),
                .pid         = whole(object, "pid"),
                .focusOrder  = whole(object, "focusHistoryID"),
            });
        }
        // Workspaces by number, special ones (negative) after the rest.
        std::ranges::sort(tree.workspaces, [](const Workspace& a, const Workspace& b) { return (a.id < 0) != (b.id < 0) ? a.id > 0 : a.id < b.id; });
        std::ranges::sort(tree.monitors, [](const Monitor& a, const Monitor& b) { return std::pair{a.x, a.y} < std::pair{b.x, b.y}; });
        return tree;
    }

}
