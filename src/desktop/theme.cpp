// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/theme.hpp"
#include "desktop/user-themes.hpp"

#include "configuration/hyprlang-edit.hpp"
#include "desktop/process.hpp"
#include "desktop/system.hpp"
#include "desktop/window-look.hpp"
#include "configuration/lua-edit.hpp"
#include "syntax/highlight.hpp"

#include <array>
#include <format>
#include <functional>
#include <fstream>
#include <sstream>

namespace desktop {

    namespace {

        // #rrggbb as Hyprland's rgba(rrggbbaa).
        std::string rgba(std::string_view hex, std::string_view alpha) {
            return std::format("rgba({}{})", hex.substr(1), alpha);
        }

        // #rrggbb as hyprlang's 0xaarrggbb.
        std::string argb(std::string_view hex) {
            return std::format("0xff{}", hex.substr(1));
        }

        std::string readAll(const std::filesystem::path& file) {
            std::ifstream     stream{file, std::ios::binary};
            std::stringstream text;
            text << stream.rdbuf();
            return text.str();
        }

        std::filesystem::path configHome(const foundation::Environment& environment) {
            if (const auto home = foundation::variable(environment, "XDG_CONFIG_HOME"); home && std::filesystem::path{*home}.is_absolute())
                return *home;
            return std::filesystem::path{foundation::variable(environment, "HOME").value_or("")} / ".config";
        }

        bool installed(std::string_view program, const foundation::Environment& environment) {
            return findProgram(program, foundation::variable(environment, "PATH").value_or("")).has_value();
        }

        using configuration::planEdit;

        const std::vector<std::string_view> activePath{"general", "col", "active_border"};
        const std::vector<std::string_view> inactivePath{"general", "col", "inactive_border"};

    }

    std::string activeBorder(const Palette& palette) {
        return std::format("{{ colors = {{\"{}\", \"{}\"}}, angle = 45 }}", rgba(palette.accent, "ee"), rgba(palette.audio, "ee"));
    }

    std::string inactiveBorder(const Palette& palette) {
        return std::format("\"{}\"", rgba(palette.raised, "aa"));
    }

    std::vector<std::pair<std::string, std::string>> hyprtoolkitColors(const Palette& palette) {
        return {
            {"background", argb(palette.base)},       {"base", argb(palette.surface)},         {"alternate_base", argb(palette.raised)},
            {"text", argb(palette.text)},             {"bright_text", argb(palette.text)},     {"link_text", argb(palette.audio)},
            {"accent", argb(palette.accent)},         {"accent_secondary", argb(palette.network)},
        };
    }

    std::string wofiStyle(const Palette& palette) {
        return std::format(R"(/* {} */

window {{
    margin: 0;
    border: 2px solid {};
    border-radius: 10px;
    background-color: {};
    font-family: "Adwaita Sans";
}}

#input {{
    margin: 8px;
    padding: 6px 10px;
    border: none;
    border-radius: 7px;
    color: {};
    background-color: {};
}}

#outer-box {{
    margin: 4px;
}}

#scroll {{
    margin: 0 8px 8px 8px;
}}

#text {{
    color: {};
}}

#entry {{
    padding: 4px 8px;
    border-radius: 7px;
}}

#entry:selected {{
    background-color: {};
}}

#entry:selected #text {{
    color: {};
}}
)",
                           palette.name, palette.accent, palette.base, palette.text, palette.surface, palette.text, palette.accent, palette.onAccent);
    }

    std::string gtkStyle(const Palette& palette) {
        return std::format(R"(/* {}, for GTK apps. */

@define-color base      {};
@define-color on_accent {};
@define-color surface   {};
@define-color raised    {};
@define-color subtle    {};
@define-color text      {};
@define-color accent    {};
@define-color alert     {};

/* The names apps and themes look colors up by. */
@define-color theme_bg_color          @base;
@define-color theme_fg_color          @text;
@define-color theme_base_color        @surface;
@define-color theme_text_color        @text;
@define-color theme_selected_bg_color @accent;
@define-color theme_selected_fg_color @on_accent;
@define-color window_bg_color         @base;
@define-color window_fg_color         @text;
@define-color view_bg_color           @surface;
@define-color view_fg_color           @text;
@define-color accent_bg_color         @accent;
@define-color accent_fg_color         @on_accent;
@define-color accent_color            @accent;
@define-color headerbar_bg_color      @surface;
@define-color headerbar_fg_color      @text;
@define-color popover_bg_color        @base;
@define-color popover_fg_color        @text;
@define-color card_bg_color           @surface;
@define-color card_fg_color           @text;

window,
dialog,
.background {{
    background-color: @base;
    color: @text;
}}

headerbar,
.titlebar {{
    background: @surface;
    color: @text;
    border-color: @raised;
    box-shadow: none;
}}

button {{
    background: @surface;
    color: @text;
    border-color: @raised;
    box-shadow: none;
    text-shadow: none;
}}

button:hover {{
    background: @raised;
}}

button:checked,
button.suggested-action {{
    background: @accent;
    color: @on_accent;
}}

button.flat {{
    background: transparent;
    border-color: transparent;
}}

button.flat:hover {{
    background: @surface;
}}

entry,
spinbutton,
textview text,
treeview,
list,
listview,
.view {{
    background-color: @surface;
    color: @text;
    border-color: @raised;
}}

combobox button,
dropdown > button {{
    background: @surface;
    color: @text;
}}

row:selected,
treeview:selected,
selection,
*:selected {{
    background-color: @accent;
    color: @on_accent;
}}

scale trough,
progressbar trough,
levelbar trough {{
    background-color: @raised;
}}

scale highlight,
progressbar progress,
levelbar block.filled {{
    background-color: @accent;
}}

scale slider {{
    background: @text;
    border-color: @raised;
}}

notebook > header,
notebook header {{
    background-color: @base;
    border-color: @raised;
}}

notebook tab {{
    color: @subtle;
}}

notebook tab:checked {{
    color: @text;
    box-shadow: inset 0 -3px @accent;
}}

popover contents,
popover > arrow,
popover.background > contents,
menu,
.menu {{
    background-color: @base;
    color: @text;
}}

check,
radio {{
    background: @surface;
    border-color: @raised;
}}

check:checked,
radio:checked,
switch:checked {{
    background: @accent;
    color: @on_accent;
}}

switch {{
    background: @raised;
}}

.dim-label,
label.dim-label {{
    color: @subtle;
}}

.error {{
    color: @alert;
}}

/* What a high-contrast theme draws light whatever the palette. */
toolbar,
.toolbar,
.inline-toolbar,
actionbar,
actionbar > revealer > box,
statusbar,
searchbar,
searchbar > revealer > box {{
    background: @base;
    color: @text;
    border-color: @raised;
}}

toolbar button,
.inline-toolbar button,
actionbar button {{
    background: transparent;
    border-color: transparent;
    color: @text;
}}

toolbar button:hover,
.inline-toolbar button:hover,
actionbar button:hover {{
    background: @surface;
}}

frame > border,
.frame,
scrolledwindow,
viewport {{
    border-color: @raised;
}}

separator {{
    background: @raised;
}}

scrollbar,
scrollbar trough {{
    background: transparent;
    border-color: transparent;
}}

scrollbar slider {{
    background: @raised;
}}

image,
label {{
    color: inherit;
}}
)",
                           palette.name, palette.base, palette.onAccent, palette.surface, palette.raised, palette.subtle, palette.text, palette.accent, palette.alert);
    }

    std::vector<GSetting> gtkChanges(const Palette& palette, const foundation::Environment& environment) {
        return gsettingChanges({{"org.gnome.desktop.interface", "color-scheme", "", palette.dark ? "'prefer-dark'" : "'prefer-light'"},
                                {"org.gnome.desktop.interface", "accent-color", "", std::format("'{}'", palette.gnomeAccent)}},
                               environment);
    }

    std::filesystem::path hyprtoolkitConfigFile(const foundation::Environment& environment) {
        if (const auto home = foundation::variable(environment, "XDG_CONFIG_HOME"); home && std::filesystem::path{*home}.is_absolute())
            return std::filesystem::path{*home} / "hypr" / "hyprtoolkit.conf";
        return std::filesystem::path{foundation::variable(environment, "HOME").value_or("")} / ".config" / "hypr" / "hyprtoolkit.conf";
    }

    std::expected<std::vector<configuration::FileChange>, std::string> planTheme(const Palette& palette, WaybarStyle bar, const configuration::MainConfig& config,
                                                                                 const foundation::Environment& environment) {
        if (!config.lua())
            return std::unexpected{"the theme changes window borders only in a Lua config (hyprland.lua) so far"};
        std::vector<configuration::FileChange> changes;

        std::error_code error;
        const auto      file = std::filesystem::canonical(config.file, error);
        if (error)
            return std::unexpected{std::format("cannot read {}: {}", config.file.string(), error.message())};
        auto hyprland = configuration::planFile(file, "");
        if (!hyprland)
            return std::unexpected{hyprland.error()};
        auto text = configuration::withLuaConfigValue(hyprland->current.value_or(""), activePath, activeBorder(palette));
        if (text)
            text = configuration::withLuaConfigValue(*text, inactivePath, inactiveBorder(palette));
        if (!text)
            return std::unexpected{text.error()};
        hyprland->proposed = std::move(*text);
        if (hyprland->changes())
            changes.push_back(std::move(*hyprland));

        bar.palette  = &palette;
        bar.geometry = barGeometry(config);
        auto waybar  = planWaybarStyle(bar, environment);
        if (!waybar)
            return std::unexpected{waybar.error()};
        for (auto& change : *waybar)
            if (change.changes())
                changes.push_back(std::move(change));

        auto toolkit = configuration::planFile(hyprtoolkitConfigFile(environment), "");
        if (!toolkit)
            return std::unexpected{toolkit.error()};
        toolkit->proposed = configuration::withHyprlangValues(toolkit->current.value_or(""), hyprtoolkitColors(palette));
        if (toolkit->changes())
            changes.push_back(std::move(*toolkit));

        const auto home = configHome(environment);
        std::vector<std::expected<std::optional<configuration::FileChange>, std::string>> more;
        // GTK apps, such as the volume control and the network settings: GTK 4 and GTK 3
        // each read their own stylesheet, and GTK 3 its dark or light preference from
        // settings.ini rather than from dconf.
        for (const auto* version : {"gtk-3.0", "gtk-4.0"})
            more.push_back(planEdit(home / version / "gtk.css", [&palette](std::string_view) { return gtkStyle(palette); }));
        more.push_back(planEdit(home / "gtk-3.0" / "settings.ini", [&palette](std::string_view text) {
            return configuration::withIniValues(text, {{"Settings", "gtk-application-prefer-dark-theme", palette.dark ? "1" : "0"}});
        }));
        if (installed("wofi", environment))
            more.push_back(planEdit(home / "wofi" / "style.css", [&palette](std::string_view) { return wofiStyle(palette); }));
        if (installed("dunst", environment)) {
            // A dunstrc of the user's own replaces the system's whole, so the first one starts as a copy of it.
            std::string system;
            for (const auto* candidate : {"/etc/xdg/dunst/dunstrc", "/etc/dunst/dunstrc"})
                if (system.empty())
                    system = readAll(candidate);
            const auto quoted = [](std::string_view hex) { return std::format("\"{}\"", hex); };
            const std::vector<configuration::IniValue> colors{
                {"global", "frame_color", quoted(palette.accent)},
                {"urgency_low", "background", quoted(palette.base)},       {"urgency_low", "foreground", quoted(palette.subtle)},
                {"urgency_low", "frame_color", quoted(palette.raised)},    {"urgency_normal", "background", quoted(palette.base)},
                {"urgency_normal", "foreground", quoted(palette.text)},    {"urgency_normal", "frame_color", quoted(palette.accent)},
                {"urgency_critical", "background", quoted(palette.base)},  {"urgency_critical", "foreground", quoted(palette.text)},
                {"urgency_critical", "frame_color", quoted(palette.alert)},
            };
            more.push_back(planEdit(home / "dunst" / "dunstrc", [&colors](std::string_view text) { return configuration::withIniValues(text, colors); }, system));
        }
        for (auto& planned : more) {
            if (!planned)
                return std::unexpected{planned.error()};
            if (*planned)
                changes.push_back(std::move(**planned));
        }
        return changes;
    }

    const Palette* currentTheme(const configuration::MainConfig& config) {
        if (!config.lua())
            return nullptr;
        const auto source = readAll(config.file);
        const auto active = syntax::luaCallField(source, "hl.config", activePath);
        if (!active)
            return nullptr;
        const auto shown = std::string_view{source}.substr(active->begin, active->end - active->begin);
        for (const auto* palette : allPalettes())
            if (shown == activeBorder(*palette))
                return palette;
        return nullptr;
    }

}
