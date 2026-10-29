// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/essentials.hpp"

#include "desktop/process.hpp"

#include <array>
#include <format>
#include <fstream>
#include <string_view>
#include <system_error>

namespace desktop {

    namespace {

        struct Program {
            std::string_view path;
            std::string_view package;
        };

        // The polkit agents Arch packages, the one made for Hyprland first.
        constexpr std::array polkitAgents{
            Program{"/usr/lib/hyprpolkitagent/hyprpolkitagent", "hyprpolkitagent"},
            Program{"/usr/lib/polkit-kde-authentication-agent-1", "polkit-kde-agent"},
            Program{"/usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1", "polkit-gnome"},
            Program{"/usr/lib/mate-polkit/polkit-mate-authentication-agent-1", "mate-polkit"},
            Program{"/usr/bin/lxqt-policykit-agent", "lxqt-policykit"},
        };

        constexpr std::array notificationDaemons{"mako", "dunst", "swaync", "fnott", "hyprnotify"};

        // `path`, an absolute one, inside `root`.
        std::filesystem::path under(const std::filesystem::path& root, std::string_view path) {
            return root / std::filesystem::path{path}.relative_path();
        }

        bool running(const EssentialSources& sources, const std::filesystem::path& program) {
            // The system keeps at most 15 characters of a process's name.
            return !findProcesses(program.filename().string().substr(0, 15), sources.processes).empty();
        }

        bool fileContains(const std::filesystem::path& file, std::string_view text) {
            std::ifstream stream{file};
            for (std::string line; std::getline(stream, line);)
                if (line.contains(text))
                    return true;
            return false;
        }

        // The first file in `folder` that has `text` on one of its lines, skipping `except`.
        std::optional<std::filesystem::path> fileWith(const std::filesystem::path& folder, std::string_view text, std::string_view except = {}) {
            std::error_code error;
            for (const auto& entry : std::filesystem::directory_iterator{folder, error})
                if (entry.path().filename() != except && fileContains(entry.path(), text))
                    return entry.path();
            return std::nullopt;
        }

        std::optional<std::string> firstFont(const EssentialSources& sources, std::initializer_list<const char*> families) {
            for (const auto* family : families)
                if (sources.hasFont(family))
                    return family;
            return std::nullopt;
        }

        Essential screenSharing(const EssentialSources& sources) {
            Essential essential{.requirement = {.name     = "Screen sharing",
                                                .purpose  = "Lets browsers, OBS and video calls share your screen or a window, and apps take screenshots, through Hyprland.",
                                                .upstream = "https://github.com/hyprwm/xdg-desktop-portal-hyprland",
                                                .package  = "xdg-desktop-portal-hyprland",
                                                .found    = std::nullopt},
                                .problem     = {},
                                .start       = std::nullopt};
            if (const auto portal = under(sources.root, "/usr/share/xdg-desktop-portal/portals/hyprland.portal"); std::filesystem::exists(portal))
                essential.requirement.found = "xdg-desktop-portal-hyprland";
            return essential;
        }

        Essential fileDialogs(const EssentialSources& sources) {
            Essential essential{.requirement = {.name     = "Open and save dialogs",
                                                .purpose  = "Gives browsers and Flatpak apps a window to pick files with, and tells apps whether you prefer dark mode. "
                                                            "Hyprland's own portal has no file dialog.",
                                                .upstream = "https://github.com/flatpak/xdg-desktop-portal-gtk",
                                                .package  = "xdg-desktop-portal-gtk",
                                                .found    = std::nullopt},
                                .problem     = {},
                                .start       = std::nullopt};
            if (const auto portal = fileWith(under(sources.root, "/usr/share/xdg-desktop-portal/portals"), "org.freedesktop.impl.portal.FileChooser", "hyprland.portal"))
                essential.requirement.found = std::format("the {} portal", portal->stem().string());
            return essential;
        }

        Essential passwordPrompts(const EssentialSources& sources) {
            Essential essential{.requirement = {.name     = "Password prompts",
                                                .purpose  = "A polkit agent asks for your password when an app needs to change the system, such as mounting a "
                                                            "disk or installing an update. Without one running, those apps fail without asking.",
                                                .upstream = "https://github.com/hyprwm/hyprpolkitagent",
                                                .package  = "hyprpolkitagent",
                                                .found    = std::nullopt},
                                .problem     = {},
                                .start       = std::nullopt};
            std::optional<std::filesystem::path> installed;
            for (const auto& agent : polkitAgents) {
                if (!std::filesystem::exists(under(sources.root, agent.path)))
                    continue;
                if (running(sources, agent.path)) {
                    essential.requirement.found = std::format("{}, running", agent.package);
                    return essential;
                }
                if (!installed) {
                    installed                   = agent.path;
                    essential.requirement.found = std::format("{}, installed", agent.package);
                }
            }
            if (installed) {
                essential.problem = "It is installed but not running, so apps that need your password cannot ask for it.";
                essential.start               = installed;
            }
            return essential;
        }

        Essential notifications(const EssentialSources& sources) {
            Essential essential{.requirement = {.name     = "Notifications",
                                                .purpose  = "Shows the pop-ups apps send, such as a finished download or a new message. Without a notification "
                                                            "daemon they are lost.",
                                                .upstream = "https://github.com/emersion/mako",
                                                .package  = "mako",
                                                .found    = std::nullopt},
                                .problem     = {},
                                .start       = std::nullopt};
            for (const auto* daemon : notificationDaemons)
                if (running(sources, daemon)) {
                    essential.requirement.found = std::format("{}, running", daemon);
                    return essential;
                }
            // One D-Bus starts on the first notification.
            if (const auto service = fileWith(under(sources.root, "/usr/share/dbus-1/services"), "Name=org.freedesktop.Notifications"))
                essential.requirement.found = std::format("{}, started when needed", service->stem().string());
            return essential;
        }

        Essential textFont(const EssentialSources& sources) {
            Essential essential{.requirement = {.name     = "A font for text",
                                                .purpose  = "Apps and web pages need a font with the common scripts. Without one, letters fall back to whatever "
                                                            "font is left, or show as boxes.",
                                                .upstream = "https://fonts.google.com/noto",
                                                .package  = "noto-fonts",
                                                .found    = firstFont(sources, {"Noto Sans", "DejaVu Sans", "Liberation Sans", "IBM Plex Sans", "Inter", "Cantarell"})},
                                .problem     = {},
                                .start       = std::nullopt};
            return essential;
        }

        Essential emojiFont(const EssentialSources& sources) {
            return Essential{.requirement = {.name     = "Emoji",
                                             .purpose  = "Emoji in chats, web pages and file names. Without a color emoji font they show as empty boxes.",
                                             .upstream = "https://github.com/googlefonts/noto-emoji",
                                             .package  = "noto-fonts-emoji",
                                             .found    = firstFont(sources, {"Noto Color Emoji", "Twemoji", "JoyPixels"})},
                             .problem     = {},
                                .start       = std::nullopt};
        }

        Essential qtWayland(const EssentialSources& sources) {
            Essential essential{.requirement = {.name     = "Qt apps on Wayland",
                                                .purpose  = "Lets Qt 6 apps, such as KDE's and OBS, run as Wayland windows. Without it they run through XWayland, "
                                                            "blurry when the display is scaled.",
                                                .upstream = "https://doc.qt.io/qt-6/wayland-and-qt.html",
                                                .package  = "qt6-wayland",
                                                .found    = std::nullopt},
                                .problem     = {},
                                .start       = std::nullopt};
            for (const auto* plugin : {"/usr/lib/qt6/plugins/platforms/libqwayland.so", "/usr/lib/qt6/plugins/platforms/libqwayland-generic.so"})
                if (std::filesystem::exists(under(sources.root, plugin)))
                    essential.requirement.found = "qt6-wayland";
            return essential;
        }

    }

    std::vector<Essential> essentials(const EssentialSources& sources) {
        return {screenSharing(sources), fileDialogs(sources), passwordPrompts(sources), notifications(sources), textFont(sources), emojiFont(sources), qtWayland(sources)};
    }

}
