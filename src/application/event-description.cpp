// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/event-description.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <vector>

namespace application {

    namespace {

        // The event's data split at its first `count - 1` commas: a title, which comes last,
        // may have commas of its own.
        std::vector<std::string> fields(std::string_view data, std::size_t count) {
            std::vector<std::string> made;
            while (made.size() + 1 < count) {
                const auto comma = data.find(',');
                if (comma == std::string_view::npos)
                    break;
                made.emplace_back(data.substr(0, comma));
                data.remove_prefix(comma + 1);
            }
            made.emplace_back(data);
            made.resize(count);
            return made;
        }

        // Addresses come as "55ce692cd6e0" in events and "0x55ce692cd6e0" in replies.
        std::string window(std::string_view address, const std::function<std::optional<std::string>(std::string_view)>& windowName) {
            const auto withPrefix = address.starts_with("0x") ? std::string{address} : "0x" + std::string{address};
            if (const auto name = windowName(withPrefix))
                return *name;
            return std::format("window {}", withPrefix);
        }

        std::string workspace(std::string_view name) {
            if (name.starts_with("special:"))
                return std::format("special workspace {}", name.substr(8));
            return std::format("workspace {}", name);
        }

    }

    bool repeatsNewerEvent(std::string_view name) {
        constexpr std::array older{"workspace",       "focusedmon",       "activewindow",  "createworkspace", "destroyworkspace", "moveworkspace",
                                   "movewindow",      "windowtitle",      "monitoradded",  "monitorremoved",  "activespecial"};
        return std::ranges::contains(older, name);
    }

    std::string describeEvent(const ipc::Event& event, const std::function<std::optional<std::string>(std::string_view)>& windowName) {
        const auto& name = event.name;
        const auto& data = event.data;
        const auto  of   = [&](std::size_t count) { return fields(data, count); };

        if (name == "openwindow") {
            const auto parts = of(4);
            return std::format("{} opened on {}{}", parts[2].empty() ? "A window" : parts[2], workspace(parts[1]), parts[3].empty() ? "" : std::format(": \"{}\"", parts[3]));
        }
        if (name == "closewindow")
            return std::format("{} closed", window(data, windowName));
        if (name == "activewindowv2")
            return data.empty() || data == "," ? "No window has focus" : std::format("{} has focus", window(data, windowName));
        if (name == "activewindow") {
            const auto parts = of(2);
            return parts[0].empty() ? "No window has focus" : std::format("{} has focus", parts[0]);
        }
        if (name == "workspacev2" || name == "workspace")
            return std::format("Switched to {}", workspace(name == "workspace" ? data : of(2)[1]));
        if (name == "focusedmonv2" || name == "focusedmon") {
            const auto parts = of(2);
            return std::format("Monitor {} has focus", parts[0]);
        }
        if (name == "createworkspacev2" || name == "createworkspace")
            return std::format("{} created", workspace(name == "createworkspace" ? data : of(2)[1]));
        if (name == "destroyworkspacev2" || name == "destroyworkspace")
            return std::format("{} removed, now empty", workspace(name == "destroyworkspace" ? data : of(2)[1]));
        if (name == "moveworkspacev2") {
            const auto parts = of(3);
            return std::format("{} moved to monitor {}", workspace(parts[1]), parts[2]);
        }
        if (name == "moveworkspace") {
            const auto parts = of(2);
            return std::format("{} moved to monitor {}", workspace(parts[0]), parts[1]);
        }
        if (name == "renameworkspace") {
            const auto parts = of(2);
            return std::format("Workspace {} renamed to {}", parts[0], parts[1]);
        }
        if (name == "movewindowv2") {
            const auto parts = of(3);
            return std::format("{} moved to {}", window(parts[0], windowName), workspace(parts[2]));
        }
        if (name == "movewindow") {
            const auto parts = of(2);
            return std::format("{} moved to {}", window(parts[0], windowName), workspace(parts[1]));
        }
        if (name == "changefloatingmode") {
            const auto parts = of(2);
            return std::format("{} {}", window(parts[0], windowName), parts[1] == "1" ? "floats" : "tiles");
        }
        if (name == "fullscreen")
            return data == "1" ? "A window went fullscreen" : "A window left fullscreen";
        if (name == "windowtitlev2") {
            const auto parts = of(2);
            return std::format("{} is now titled \"{}\"", window(parts[0], windowName), parts[1]);
        }
        if (name == "windowtitle")
            return std::format("{} changed its title", window(data, windowName));
        if (name == "monitoraddedv2") {
            const auto parts = of(3);
            return std::format("Monitor {} plugged in{}", parts[1], parts[2].empty() ? "" : std::format(": {}", parts[2]));
        }
        if (name == "monitoradded")
            return std::format("Monitor {} plugged in", data);
        if (name == "monitorremovedv2") {
            const auto parts = of(3);
            return std::format("Monitor {} unplugged", parts[1]);
        }
        if (name == "monitorremoved")
            return std::format("Monitor {} unplugged", data);
        if (name == "activespecialv2" || name == "activespecial") {
            const auto parts = name == "activespecial" ? of(2) : of(3);
            const auto shown = name == "activespecial" ? parts[0] : parts[1];
            const auto where = name == "activespecial" ? parts[1] : parts[2];
            return shown.empty() ? std::format("Special workspace hidden on {}", where) : std::format("{} shown on {}", workspace(shown), where);
        }
        if (name == "activelayout") {
            const auto parts = of(2);
            return std::format("Keyboard layout is now {}", parts[1]);
        }
        if (name == "submap")
            return data.empty() ? "Back to the normal keybindings" : std::format("Keybindings switched to submap {}", data);
        if (name == "openlayer")
            return std::format("{} appeared (a panel, bar or overlay)", data);
        if (name == "closelayer")
            return std::format("{} went away", data);
        if (name == "configreloaded")
            return "Hyprland read its config again";
        if (name == "urgent")
            return std::format("{} wants attention", window(data, windowName));
        if (name == "minimized") {
            const auto parts = of(2);
            return std::format("{} {}", window(parts[0], windowName), parts[1] == "1" ? "was minimized" : "was restored");
        }
        if (name == "pin") {
            const auto parts = of(2);
            return std::format("{} {}", window(parts[0], windowName), parts[1] == "1" ? "pinned to every workspace" : "unpinned");
        }
        if (name == "screencast") {
            const auto parts = of(2);
            return parts[0] == "1" ? std::format("Screen sharing started ({})", parts[1] == "1" ? "a window" : "a monitor") : "Screen sharing stopped";
        }
        if (name == "bell")
            return std::format("{} rang the bell", data.empty() ? "Something" : window(data, windowName));
        if (name == "togglegroup") {
            const auto parts = of(2);
            return parts[0] == "1" ? "A window group was made" : "A window group was taken apart";
        }
        if (name == "moveintogroup")
            return std::format("{} joined a group", window(data, windowName));
        if (name == "moveoutofgroup")
            return std::format("{} left its group", window(data, windowName));
        if (name == "lockgroups")
            return data == "1" ? "Window groups locked" : "Window groups unlocked";
        if (name == "ignoregrouplock")
            return data == "1" ? "Group locks ignored" : "Group locks obeyed";
        return data.empty() ? name : std::format("{}: {}", name, data);
    }

}
