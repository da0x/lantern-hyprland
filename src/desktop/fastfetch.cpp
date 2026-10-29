// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/fastfetch.hpp"

#include <algorithm>
#include <charconv>
#include <format>

#include <nlohmann/json.hpp>

namespace desktop {

    namespace {

        constexpr std::string_view marker   = "// Written by Lantern for Hyprland, in the terminal's own colors.";
        constexpr std::string_view greeting = "command -v fastfetch >/dev/null && fastfetch";
        constexpr std::string_view about    = "# Lantern for Hyprland: a summary of the system in each new terminal.";

        constexpr std::array<std::string_view, 9>  shortModules{"title", "separator", "os", "kernel", "wm", "uptime", "packages", "memory", "colors"};
        constexpr std::array<std::string_view, 22> fullModules{"title", "separator", "os",   "host", "kernel", "uptime", "packages", "shell",  "display", "wm",    "theme",
                                                               "icons", "font",      "cursor", "terminal", "cpu", "gpu",      "memory", "disk",   "locale",  "break", "colors"};

        std::string_view trim(std::string_view text) {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
                text.remove_suffix(1);
            return text;
        }

        std::filesystem::path configHome(const foundation::Environment& environment) {
            if (const auto home = foundation::variable(environment, "XDG_CONFIG_HOME"); home && std::filesystem::path{*home}.is_absolute())
                return *home;
            return std::filesystem::path{foundation::variable(environment, "HOME").value_or("")} / ".config";
        }

    }

    std::filesystem::path fastfetchConfigFile(const foundation::Environment& environment) {
        return configHome(environment) / "fastfetch" / "config.jsonc";
    }

    std::string fastfetchConfig(const Fastfetch& fetch) {
        nlohmann::ordered_json config;
        config["$schema"] = "https://github.com/fastfetch-cli/fastfetch/raw/dev/doc/json_schema.json";
        if (fetch.logo == FetchLogo::none)
            config["logo"] = {{"type", "none"}};
        else
            config["logo"] = {{"type", fetch.logo == FetchLogo::small ? "small" : "builtin"}, {"color", {{"1", fetch.accent}, {"2", fetch.accent}}}};
        config["display"] = {{"separator", "  "}, {"color", {{"keys", fetch.accent}, {"title", fetch.accent}}}};
        auto& modules     = config["modules"] = nlohmann::ordered_json::array();
        if (fetch.full)
            for (const auto module : fullModules)
                modules.push_back(module);
        else
            for (const auto module : shortModules)
                modules.push_back(module);
        return std::format("{}\n{}\n", marker, config.dump(4));
    }

    std::optional<Fastfetch> fastfetchIn(std::string_view config) {
        if (!config.starts_with(marker))
            return std::nullopt;
        const auto json = nlohmann::json::parse(config, nullptr, false, true);
        if (json.is_discarded() || !json.is_object())
            return std::nullopt;
        Fastfetch fetch;
        const auto type = json.contains("logo") && json["logo"].is_object() ? json["logo"].value("type", "builtin") : std::string{"builtin"};
        fetch.logo      = type == "none" ? FetchLogo::none : type == "small" ? FetchLogo::small : FetchLogo::large;
        if (json.contains("display") && json["display"].contains("color") && json["display"]["color"].is_object())
            fetch.accent = json["display"]["color"].value("keys", fetch.accent);
        fetch.full = json.contains("modules") && json["modules"].is_array() && json["modules"].size() > shortModules.size();
        return fetch;
    }

    std::filesystem::path bashrcFile(const foundation::Environment& environment) {
        return std::filesystem::path{foundation::variable(environment, "HOME").value_or("")} / ".bashrc";
    }

    bool greetsWithFastfetch(std::string_view bashrc) {
        std::size_t at = 0;
        while (at <= bashrc.size()) {
            const auto end  = std::min(bashrc.find('\n', at), bashrc.size());
            const auto line = trim(bashrc.substr(at, end - at));
            if (line == greeting || line == "fastfetch")
                return true;
            at = end + 1;
        }
        return false;
    }

    std::string withFastfetchGreeting(std::string_view bashrc, bool on) {
        std::string text;
        std::size_t at = 0;
        while (at < bashrc.size()) {
            const auto newline = bashrc.find('\n', at);
            const auto stop    = newline == std::string_view::npos ? bashrc.size() : newline;
            const auto end     = newline == std::string_view::npos ? bashrc.size() : newline + 1;
            const auto line    = trim(bashrc.substr(at, stop - at));
            if (line == about) {
                // With the blank line it was added after.
                if (text.ends_with("\n\n"))
                    text.pop_back();
            } else if (line != greeting && line != "fastfetch")
                text += bashrc.substr(at, end - at);
            at = end;
        }
        if (on) {
            if (!text.empty() && !text.ends_with('\n'))
                text += '\n';
            text += std::format("\n{}\n{}\n", about, greeting);
        }
        return text;
    }


    std::vector<FetchRun> fetchRuns(std::string_view line) {
        std::vector<FetchRun> runs;
        FetchRun              run;
        std::size_t           at = 0;
        while (at < line.size()) {
            if (!line.substr(at).starts_with("\x1b[")) {
                run.text += line[at++];
                continue;
            }
            const auto end = line.find_first_not_of("0123456789;", at + 2);
            if (end == std::string_view::npos)
                break;
            if (!run.text.empty())
                runs.push_back(run);
            run.text.clear();
            if (line[end] == 'm') {
                const auto codes = line.substr(at + 2, end - at - 2);
                std::size_t from = 0;
                while (from <= codes.size()) {
                    const auto stop = std::min(codes.find(';', from), codes.size());
                    int        code = 0;
                    (void)std::from_chars(codes.data() + from, codes.data() + stop, code);
                    if (code == 0)
                        run.foreground = run.background = std::nullopt;
                    else if (code >= 30 && code <= 37)
                        run.foreground = code - 30;
                    else if (code >= 90 && code <= 97)
                        run.foreground = code - 90 + 8;
                    else if (code == 39)
                        run.foreground = std::nullopt;
                    else if (code >= 40 && code <= 47)
                        run.background = code - 40;
                    else if (code >= 100 && code <= 107)
                        run.background = code - 100 + 8;
                    else if (code == 49)
                        run.background = std::nullopt;
                    from = stop + 1;
                }
            }
            at = end + 1;
        }
        if (!run.text.empty())
            runs.push_back(run);
        return runs;
    }

}
