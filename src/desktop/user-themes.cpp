// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/user-themes.hpp"

#include "desktop/color.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <format>
#include <fstream>
#include <ranges>
#include <sstream>

namespace desktop {

    namespace {

        const std::array<DraftColor, 11> colors{{
            {&ThemeDraft::base, "Background", "the bar and the windows' backdrop"},
            {&ThemeDraft::surface, "Surface", "borders and what is hovered"},
            {&ThemeDraft::raised, "Raised", "the inactive windows' border and cards"},
            {&ThemeDraft::muted, "Muted", "what is off, such as muted sound"},
            {&ThemeDraft::subtle, "Subtle", "quieter text"},
            {&ThemeDraft::text, "Text", "text on the background"},
            {&ThemeDraft::accent, "Accent", "the focused window and what is chosen"},
            {&ThemeDraft::onAccent, "On the accent", "text on the accent"},
            {&ThemeDraft::audio, "Second accent", "the focused window's gradient and links"},
            {&ThemeDraft::network, "Third accent", "the network on the bar"},
            {&ThemeDraft::alert, "Alert", "what is urgent"},
        }};

        bool isHex(std::string_view value) {
            return value.size() == 7 && value.front() == '#' && std::ranges::all_of(value.substr(1), [](char character) { return std::isxdigit(static_cast<unsigned char>(character)) != 0; });
        }

        std::string_view trim(std::string_view text) {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
                text.remove_suffix(1);
            return text;
        }

        // A palette of the user's, with the strings its views point into. Kept in a deque,
        // which never moves what it holds, and never erased.
        struct Stored {
            ThemeDraft            draft;
            std::string           gnome;
            std::filesystem::path file;
            Palette               palette;
        };

        std::deque<Stored>& stored() {
            static std::deque<Stored> all;
            return all;
        }

        std::vector<const Stored*>& loaded() {
            static std::vector<const Stored*> current;
            return current;
        }

        std::string slugOf(std::string_view name) {
            std::string slug;
            for (const auto character : name) {
                if (std::isalnum(static_cast<unsigned char>(character)) != 0)
                    slug += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
                else if (!slug.empty() && slug.back() != '-')
                    slug += '-';
            }
            while (slug.ends_with('-'))
                slug.pop_back();
            return slug.empty() ? "theme" : slug;
        }

    }

    std::span<const DraftColor> draftColors() {
        return colors;
    }

    ThemeDraft draftOf(const Palette& palette) {
        return ThemeDraft{.name     = std::string{palette.name},
                          .dark     = palette.dark,
                          .base     = std::string{palette.base},
                          .onAccent = std::string{palette.onAccent},
                          .surface  = std::string{palette.surface},
                          .raised   = std::string{palette.raised},
                          .muted    = std::string{palette.muted},
                          .subtle   = std::string{palette.subtle},
                          .text     = std::string{palette.text},
                          .accent   = std::string{palette.accent},
                          .audio    = std::string{palette.audio},
                          .network  = std::string{palette.network},
                          .alert    = std::string{palette.alert}};
    }

    std::vector<std::string> draftProblems(const ThemeDraft& draft) {
        std::vector<std::string> problems;
        if (trim(draft.name).empty())
            problems.emplace_back("It needs a name.");
        for (const auto& color : colors)
            if (!isHex(draft.*color.member))
                problems.push_back(std::format("{} is not a color written as #rrggbb.", color.name));
        if (problems.empty()) {
            if (contrast(parseColor(draft.text), parseColor(draft.base)) < 4.5)
                problems.emplace_back("Text is hard to read on the background: they need more contrast.");
            if (contrast(parseColor(draft.onAccent), parseColor(draft.accent)) < 3)
                problems.emplace_back("Text on the accent is hard to read: they need more contrast.");
        }
        return problems;
    }

    std::string themeFileText(const ThemeDraft& draft) {
        std::string text = std::format("# A theme for Lantern for Hyprland.\nname = {}\ndark = {}\n", trim(draft.name), draft.dark ? "true" : "false");
        const std::array<std::pair<std::string_view, std::string ThemeDraft::*>, 11> keys{{
            {"base", &ThemeDraft::base}, {"on_accent", &ThemeDraft::onAccent}, {"surface", &ThemeDraft::surface}, {"raised", &ThemeDraft::raised},
            {"muted", &ThemeDraft::muted}, {"subtle", &ThemeDraft::subtle}, {"text", &ThemeDraft::text}, {"accent", &ThemeDraft::accent},
            {"audio", &ThemeDraft::audio}, {"network", &ThemeDraft::network}, {"alert", &ThemeDraft::alert},
        }};
        for (const auto& [key, member] : keys)
            text += std::format("{} = {}\n", key, draft.*member);
        return text;
    }

    std::optional<ThemeDraft> parseThemeFile(std::string_view text) {
        ThemeDraft draft;
        for (const auto part : text | std::views::split('\n')) {
            const auto line   = trim(std::string_view{part.begin(), part.end()});
            const auto equals = line.find('=');
            if (line.starts_with('#') || equals == std::string_view::npos)
                continue;
            const auto key   = trim(line.substr(0, equals));
            const auto value = std::string{trim(line.substr(equals + 1))};
            if (key == "name")
                draft.name = value;
            else if (key == "dark")
                draft.dark = value != "false";
            else if (key == "base")
                draft.base = value;
            else if (key == "on_accent")
                draft.onAccent = value;
            else if (key == "surface")
                draft.surface = value;
            else if (key == "raised")
                draft.raised = value;
            else if (key == "muted")
                draft.muted = value;
            else if (key == "subtle")
                draft.subtle = value;
            else if (key == "text")
                draft.text = value;
            else if (key == "accent")
                draft.accent = value;
            else if (key == "audio")
                draft.audio = value;
            else if (key == "network")
                draft.network = value;
            else if (key == "alert")
                draft.alert = value;
        }
        if (draft.name.empty() || std::ranges::any_of(colors, [&](const DraftColor& color) { return !isHex(draft.*color.member); }))
            return std::nullopt;
        return draft;
    }

    std::string_view gnomeAccentOf(std::string_view hex) {
        constexpr std::array<std::pair<std::string_view, std::string_view>, 8> accents{{
            {"blue", "#3584e4"}, {"teal", "#2190a4"}, {"green", "#3a944a"}, {"yellow", "#c88800"},
            {"orange", "#ed5b00"}, {"red", "#e62d42"}, {"pink", "#d56199"}, {"purple", "#9141ac"},
        }};
        const auto color = lchOf(parseColor(hex));
        if (color.chroma < 0.03)
            return "slate";
        std::string_view nearest  = "blue";
        double           distance = 360;
        for (const auto& [name, accent] : accents) {
            const auto apart = std::fabs(std::remainder(color.hue - lchOf(parseColor(accent)).hue, 360.0));
            if (apart < distance) {
                distance = apart;
                nearest  = name;
            }
        }
        return nearest;
    }

    std::vector<const Palette*> allPalettes() {
        std::vector<const Palette*> all;
        for (const auto& palette : waybarPalettes())
            all.push_back(&palette);
        for (const auto* user : loaded())
            all.push_back(&user->palette);
        return all;
    }

    void loadUserPalettes(const std::filesystem::path& folder) {
        std::vector<std::filesystem::path> files;
        std::error_code                    error;
        for (const auto& item : std::filesystem::directory_iterator{folder, error})
            if (item.path().extension() == ".conf" && item.is_regular_file(error))
                files.push_back(item.path());
        std::vector<const Stored*> fresh;
        for (const auto& file : files) {
            std::ifstream     stream{file, std::ios::binary};
            std::stringstream text;
            text << stream.rdbuf();
            const auto draft = parseThemeFile(text.str());
            if (!draft)
                continue;
            // The one read before, when nothing in it changed, so its pointer stays the same.
            const auto same = std::ranges::find_if(stored(), [&](const Stored& one) { return one.file == file && one.draft == *draft; });
            if (same != stored().end()) {
                fresh.push_back(&*same);
                continue;
            }
            auto& made   = stored().emplace_back();
            made.draft   = *draft;
            made.gnome   = std::string{gnomeAccentOf(draft->accent)};
            made.file    = file;
            made.palette = Palette{.name = made.draft.name, .dark = made.draft.dark, .base = made.draft.base, .onAccent = made.draft.onAccent, .surface = made.draft.surface,
                                   .raised = made.draft.raised, .muted = made.draft.muted, .subtle = made.draft.subtle, .text = made.draft.text, .accent = made.draft.accent,
                                   .audio = made.draft.audio, .network = made.draft.network, .alert = made.draft.alert, .gnomeAccent = made.gnome};
            fresh.push_back(&made);
        }
        std::ranges::sort(fresh, {}, [](const Stored* one) { return one->draft.name; });
        loaded() = std::move(fresh);
    }

    std::optional<std::filesystem::path> userThemeFile(const Palette* palette) {
        for (const auto& one : stored())
            if (&one.palette == palette)
                return one.file;
        return std::nullopt;
    }

    std::filesystem::path newThemeFile(const std::filesystem::path& folder, std::string_view name) {
        const auto slug = slugOf(name);
        auto       file = folder / std::format("{}.conf", slug);
        std::error_code error;
        for (int number = 2; std::filesystem::exists(file, error); ++number)
            file = folder / std::format("{}-{}.conf", slug, number);
        return file;
    }

}
