// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/waybar.hpp"

#include <array>
#include <format>
#include <system_error>

namespace desktop {

    namespace {

        // The family of the icons in Lantern's Waybar config.
        constexpr auto iconFamily = "Symbols Nerd Font";

    }

    std::string installCommand(const Requirement& requirement, const Distribution& distribution) {
        if (!distribution.arch())
            return {};
        return std::format("sudo pacman -S {}", requirement.package);
    }

    std::vector<Requirement> waybarRequirements(const foundation::Environment& environment) {
        Requirement waybar{
            .name     = "Waybar",
            .purpose  = "The bar at the top of the screen, with your workspaces, the clock, audio, network and the tray.",
            .upstream = "https://github.com/Alexays/Waybar",
            .package  = "waybar",
            .found    = std::nullopt,
        };
        if (const auto program = findProgram("waybar", foundation::variable(environment, "PATH").value_or("")))
            waybar.found = program->string();

        Requirement icons{
            .name     = "Icon font",
            .purpose  = "The bar's icons, for the volume and the network, come from this font. Without it they show as empty boxes.",
            .upstream = "https://www.nerdfonts.com",
            .package  = "ttf-nerd-fonts-symbols",
            .found    = std::nullopt,
        };
        if (hasFontFamily(iconFamily))
            icons.found = iconFamily;

        Requirement volume{
            .name     = "Volume control",
            .purpose  = "Clicking the volume opens pavucontrol, to pick outputs and set each app's volume.",
            .upstream = "https://freedesktop.org/software/pulseaudio/pavucontrol/",
            .package  = "pavucontrol",
            .found    = std::nullopt,
        };
        Requirement network{
            .name     = "Network settings",
            .purpose  = "Clicking the network opens NetworkManager's connection editor.",
            .upstream = "https://gitlab.gnome.org/GNOME/network-manager-applet",
            .package  = "nm-connection-editor",
            .found    = std::nullopt,
        };
        for (auto* requirement : {&volume, &network})
            if (const auto program = findProgram(requirement->package, foundation::variable(environment, "PATH").value_or("")))
                requirement->found = program->string();

        return {std::move(waybar), std::move(icons), std::move(volume), std::move(network)};
    }

    std::span<const Popup> waybarPopups() {
        static constexpr std::array popups{
            Popup{.rule = "waybar-volume", .program = "pavucontrol", .windowClass = "^(org.pulseaudio.pavucontrol)$"},
            Popup{.rule = "waybar-network", .program = "nm-connection-editor", .windowClass = "^(nm-connection-editor)$"},
        };
        return popups;
    }

    std::string withPopupRules(std::string_view config) {
        std::string text{config};
        for (const auto& popup : waybarPopups()) {
            if (text.contains(std::format("\"{}\"", popup.rule)))
                continue;
            if (!text.empty() && !text.ends_with('\n'))
                text += '\n';
            // Under the bar's right end, where the volume and the network sit.
            text += std::format(R"x(
hl.window_rule({{
    -- Opened from Waybar: floats under the bar's right end.
    name  = "{}",
    match = {{ class = "{}" }},

    float = true,
    size  = "900 600",
    move  = "monitor_w-930 70",
}})
)x",
                                popup.rule, popup.windowClass);
        }
        return text;
    }

    std::expected<std::optional<configuration::FileChange>, std::string> planPopupRules(const configuration::MainConfig& config) {
        if (!config.lua())
            return std::nullopt;
        return configuration::planEdit(config.file, [](std::string_view text) { return withPopupRules(text); });
    }

    std::optional<std::filesystem::path> waybarConfig(const foundation::Environment& environment, const std::filesystem::path& systemFolder) {
        std::vector<std::filesystem::path> folders;
        if (const auto folder = foundation::variable(environment, "WAYBAR_CONFIG_DIR"))
            folders.emplace_back(*folder);
        if (const auto home = foundation::variable(environment, "XDG_CONFIG_HOME"))
            folders.push_back(std::filesystem::path{*home} / "waybar");
        if (const auto home = foundation::variable(environment, "HOME")) {
            folders.push_back(std::filesystem::path{*home} / ".config" / "waybar");
            folders.push_back(std::filesystem::path{*home} / "waybar");
        }
        folders.push_back(systemFolder / "waybar");

        for (const auto& folder : folders) {
            for (const auto* name : {"config", "config.jsonc"}) {
                std::error_code error;
                if (const auto file = folder / name; std::filesystem::exists(file, error))
                    return file.lexically_normal();
            }
        }
        return std::nullopt;
    }

}
