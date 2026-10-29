// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screenshots-page.hpp"

#include "application/config-check.hpp"
#include "application/lantern-folder.hpp"
#include "configuration/autostart.hpp"
#include "configuration/lua-binds.hpp"
#include "configuration/main-config.hpp"
#include "desktop/clipboard.hpp"
#include "desktop/process.hpp"
#include "desktop/system.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <ranges>
#include <fstream>
#include <sstream>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr auto changeName = "Screenshots and clipboard";

        constexpr std::array<std::string_view, 3> historyKeyChoices{"SUPER + V", "SUPER + SHIFT + V", "CTRL + SUPER + V"};

        std::optional<std::filesystem::path> program(std::string_view name) {
            return desktop::findProgram(name, foundation::variable(foundation::currentEnvironment(), "PATH").value_or(""));
        }

        std::string fileText(const std::filesystem::path& file) {
            std::ifstream     stream{file, std::ios::binary};
            std::stringstream text;
            text << stream.rdbuf();
            return text.str();
        }

        // "SUPER + SHIFT + V" as people write it: Super + Shift + V.
        std::string keysText(std::string_view keys) {
            std::string text;
            for (const auto part : keys | std::views::split('+')) {
                std::string_view word{part.begin(), part.end()};
                while (word.starts_with(' '))
                    word.remove_prefix(1);
                while (word.ends_with(' '))
                    word.remove_suffix(1);
                std::string written{word};
                if (word.size() > 1) {
                    std::ranges::transform(written, written.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
                    written.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(written.front())));
                    if (written == "Control")
                        written = "Ctrl";
                }
                text += text.empty() ? written : " + " + written;
            }
            return text;
        }

        // The menu the history is picked from: the first launcher installed with a dmenu mode.
        std::optional<std::string> historyMenu() {
            for (const auto& [name, menu] : {std::pair{"wofi", "wofi --dmenu"}, std::pair{"fuzzel", "fuzzel --dmenu"}, std::pair{"rofi", "rofi -dmenu"}})
                if (program(name))
                    return std::string{menu};
            return std::nullopt;
        }

        desktop::Requirement missing(std::string name, std::string purpose, std::string upstream, std::string package) {
            return desktop::Requirement{.name = std::move(name), .purpose = std::move(purpose), .upstream = std::move(upstream), .package = std::move(package), .found = std::nullopt};
        }

    }

    ScreenshotsPage::ScreenshotsPage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop) :
        palette{std::move(palette)},
        defer{std::move(defer)},
        loop{std::move(loop)},
        diff{this->palette, this->defer,
             {RowList::Column{.width = 24, .monospace = true, .secondary = false, .markup = true, .heading = ""},
              RowList::Column{.width = 0, .monospace = true, .secondary = false, .markup = true, .heading = ""}},
             20} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        root->addChild(pageTitle("Screenshots and clipboard", this->palette));
        root->addChild(paragraph("The keys that take screenshots and where the pictures go, and a history of what you copy, to paste something "
                                 "copied before. Lantern writes the keys into Hyprland's config after you review them.",
                                 this->palette));
        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        root->addChild(body);
        footer = leftRow();
        root->addChild(footer);
        refresh();
    }

    void ScreenshotsPage::refresh() {
        if (reviewing)
            return;
        const auto config = configuration::mainConfig(configuration::lanternLaunch());
        configProblem     = !config ? std::optional{config.error()} : !config->lua() ? std::optional<std::string>{"keys are set only in a Lua config (hyprland.lua) so far"} : std::nullopt;
        configText        = config ? fileText(config->file) : "";
        shotsNow          = configProblem ? std::nullopt : desktop::screenshotsIn(configText);
        shotsWanted       = shotsNow;
        historyNow        = !configProblem && configuration::startsAtLogin(configText, true, desktop::clipboardWatcher);
        historyWanted     = historyNow;
        historyKeysNow    = configProblem ? std::nullopt : desktop::clipboardKeysIn(configText);
        historyKeysWanted = historyKeysNow.value_or("");
        if (historyKeysWanted.empty()) {
            // The first of the choices nothing is bound to.
            const auto binds = configuration::luaBindLines(configText);
            const auto free  = std::ranges::find_if(historyKeyChoices, [&](std::string_view keys) {
                return std::ranges::none_of(binds, [&](const configuration::LuaBindLine& bind) { return configuration::sameKeys(bind.keys, keys); });
            });
            historyKeysWanted = std::string{free != historyKeyChoices.end() ? *free : historyKeyChoices[1]};
        }
        showOverview();
    }

    void ScreenshotsPage::restyle() {
        if (!reviewing)
            showOverview();
    }

    std::expected<std::vector<configuration::FileChange>, std::string> ScreenshotsPage::plan() const {
        const bool shots   = shotsWanted && shotsWanted != shotsNow;
        const bool history = historyWanted != historyNow || (historyWanted && historyKeysNow != historyKeysWanted);
        if (!shots && !history)
            return std::vector<configuration::FileChange>{};
        const auto config = configuration::mainConfig(configuration::lanternLaunch());
        if (!config)
            return std::unexpected{config.error()};
        if (configProblem)
            return std::unexpected{*configProblem};
        const auto menu = historyMenu().value_or("wofi --dmenu");
        auto edited = configuration::planEdit(config->file, [&](std::string_view text) {
            std::string result{text};
            if (shots)
                result = desktop::withScreenshots(result, *shotsWanted);
            if (history) {
                if (historyKeysNow)
                    result = configuration::withoutLuaBind(result, *historyKeysNow);
                if (historyWanted) {
                    result = configuration::withAutostart(result, true, desktop::clipboardWatcher, {});
                    result = configuration::withLuaExecBind(result, historyKeysWanted, desktop::clipboardPicker(menu));
                } else
                    result = configuration::withoutAutostart(result, true, desktop::clipboardWatcher);
            }
            return result;
        });
        if (!edited)
            return std::unexpected{edited.error()};
        std::vector<configuration::FileChange> planned;
        if (*edited)
            planned.push_back(std::move(**edited));
        return planned;
    }

    void ScreenshotsPage::showOverview() {
        reviewing = false;
        body->clearChildren();
        body->addChild(space(4));
        if (configProblem) {
            body->addChild(paragraph(std::format("Lantern cannot set them: {}.", *configProblem), palette, true));
            fillStatus();
            return;
        }
        // Screenshots on the left and the clipboard on the right, each column as tall as the
        // page, so that both start at the top.
        const auto columns = leftRow(0);
        columns->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->gap(32)->commence();
        const auto left  = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(8)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {0.48, 1.0}})->commence();
        const auto right = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(8)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {0.48, 1.0}})->commence();
        columns->addChild(left);
        columns->addChild(right);
        body->addChild(columns);
        fillScreenshots(left);
        fillHistory(right);
        fillStatus();
    }

    void ScreenshotsPage::fillScreenshots(const CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column) {
        column->addChild(heading("Screenshots", palette));
        const auto copier = desktop::clipboardProgram(foundation::variable(foundation::currentEnvironment(), "PATH").value_or(""));
        if (!shotsNow) {
            column->addChild(paragraph("Your Print key does not take screenshots with grim, so Lantern leaves the keys to you unless you set them here.", palette, true));
            column->addChild(settingRow("Screenshot keys", "Print and the keys with it", {"Leave as they are", "Set them"}, shotsWanted ? 1 : 0, palette,
                                        [this](std::size_t chosen) {
                                            shotsWanted = chosen == 1 ? std::optional{desktop::Screenshots{}} : std::nullopt;
                                            showOverview();
                                        },
                                        defer));
            if (!shotsWanted)
                return;
        }
        std::string keys;
        for (const auto& key : desktop::shotKeys)
            keys += std::format("{}{}: {}", keys.empty() ? "" : ", ", keysText(key.keys), key.about);
        column->addChild(paragraph(keys + ".", palette, true));
        std::vector<std::string> folders{"~/Pictures/Screenshots", "~/Pictures"};
        if (!std::ranges::contains(folders, shotsWanted->folder))
            folders.push_back(shotsWanted->folder);
        column->addChild(settingRow("Save to", "the folder for the pictures", folders, static_cast<std::size_t>(std::ranges::find(folders, shotsWanted->folder) - folders.begin()),
                                    palette,
                                    [this, folders](std::size_t chosen) {
                                        if (chosen < folders.size())
                                            shotsWanted->folder = folders[chosen];
                                        showOverview();
                                    },
                                    defer));
        column->addChild(settingRow("Copy", "each picture to the clipboard too", {"Off", "On"}, shotsWanted->copy ? 1 : 0, palette,
                                    [this](std::size_t chosen) {
                                        shotsWanted->copy = chosen == 1;
                                        showOverview();
                                    },
                                    defer));
        column->addChild(settingRow("Draw on it", "open each picture in satty first", {"Off", "On"}, shotsWanted->annotate ? 1 : 0, palette,
                                    [this](std::size_t chosen) {
                                        shotsWanted->annotate = chosen == 1;
                                        showOverview();
                                    },
                                    defer));
        if (!program("grim") || !program("slurp"))
            column->addChild(requirementBlock(missing("grim and slurp", "Take the picture, and select the area to take.", "https://gitlab.freedesktop.org/emersion/grim",
                                                      "grim slurp"),
                                              desktop::currentDistribution(), copier, palette, defer, [this] { refresh(); }));
        if (shotsWanted->annotate && !program("satty"))
            column->addChild(requirementBlock(missing("satty", "Opens a screenshot to draw arrows, boxes and text on it before it is saved.", "https://github.com/gabm/Satty",
                                                      "satty"),
                                              desktop::currentDistribution(), copier, palette, defer, [this] { refresh(); }));
        if (shotsWanted->copy && !copier)
            column->addChild(requirementBlock(missing("wl-clipboard", "Copies the picture to the clipboard.", "https://github.com/bugaevc/wl-clipboard", "wl-clipboard"),
                                              desktop::currentDistribution(), copier, palette, defer, [this] { refresh(); }));
    }

    void ScreenshotsPage::fillHistory(const CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column) {
        column->addChild(heading("Clipboard history", palette));
        column->addChild(paragraph("Keeps what you copy, text and pictures, so a key can bring back something copied before.", palette, true));
        column->addChild(settingRow("History", "kept by cliphist from your login", {"Off", "On"}, historyWanted ? 1 : 0, palette,
                                    [this](std::size_t chosen) {
                                        historyWanted = chosen == 1;
                                        showOverview();
                                    },
                                    defer));
        if (!historyWanted)
            return;
        const auto               binds = configuration::luaBindLines(configText);
        std::vector<std::string> choices{historyKeyChoices.begin(), historyKeyChoices.end()};
        if (!std::ranges::any_of(choices, [&](const std::string& keys) { return configuration::sameKeys(keys, historyKeysWanted); }))
            choices.push_back(historyKeysWanted);
        std::vector<std::string> items;
        for (const auto& keys : choices) {
            // A key bound to something else is taken from it, and says so.
            const auto taken = std::ranges::find_if(binds, [&](const configuration::LuaBindLine& bind) {
                return configuration::sameKeys(bind.keys, keys) && !(bind.command && bind.command->contains("cliphist"));
            });
            auto label = keysText(keys);
            if (taken != binds.end()) {
                auto action = taken->command.value_or(taken->action);
                if (action.size() > 28)
                    action = action.substr(0, 27) + "…";
                label += std::format(", instead of {}", action);
            }
            items.push_back(std::move(label));
        }
        const auto current = static_cast<std::size_t>(
            std::ranges::find_if(choices, [&](const std::string& keys) { return configuration::sameKeys(keys, historyKeysWanted); }) - choices.begin());
        column->addChild(settingRow("Key", "opens the history to pick from", std::move(items), current, palette,
                                    [this, choices](std::size_t chosen) {
                                        if (chosen < choices.size())
                                            historyKeysWanted = choices[chosen];
                                        showOverview();
                                    },
                                    defer));
        const auto copier = desktop::clipboardProgram(foundation::variable(foundation::currentEnvironment(), "PATH").value_or(""));
        if (!program("cliphist"))
            column->addChild(requirementBlock(missing("cliphist", "Keeps the clipboard history.", "https://github.com/sentriz/cliphist", "cliphist"),
                                              desktop::currentDistribution(), copier, palette, defer, [this] { refresh(); }));
        if (!copier)
            column->addChild(requirementBlock(missing("wl-clipboard", "Hands cliphist what is copied, and copies what you pick.", "https://github.com/bugaevc/wl-clipboard",
                                                      "wl-clipboard"),
                                              desktop::currentDistribution(), copier, palette, defer, [this] { refresh(); }));
        if (const auto menu = historyMenu())
            column->addChild(paragraph(std::format("The history opens in {}.", menu->substr(0, menu->find(' '))), palette, true));
        else
            column->addChild(requirementBlock(missing("A launcher", "Shows the history to pick from: wofi, fuzzel or rofi.", "https://hg.sr.ht/~scoopta/wofi", "wofi"),
                                              desktop::currentDistribution(), copier, palette, defer, [this] { refresh(); }));
    }

    void ScreenshotsPage::fillStatus() {
        const auto                                               planned = plan();
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> buttons;
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> away;
        if (configuration::latestChange(application::backupsFolder(foundation::currentEnvironment()), changeName))
            away.push_back(button("Undo last change", 160, false, [this] { defer([this] { undo(); }); }));
        std::string note;
        if (!planned)
            note = escapeMarkup(std::format("Lantern cannot apply it: {}.", planned.error()));
        else if (planned->empty())
            note = outcome.empty() ? "" : std::format("{}  {}", colored("✓", hex(palette->m_colors.accent)), escapeMarkup(outcome));
        else {
            buttons.push_back(button("Review changes", 160, true, [this] { defer([this] { showReview(); }); }));
            note = escapeMarkup(outcome);
        }
        CSharedPointer<Hyprtoolkit::IElement> noteText;
        if (!note.empty()) {
            const auto made = markupLine(note, palette);
            made->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
            noteText = made;
        }
        setFooter(footer, buttons, noteText, away);
    }

    void ScreenshotsPage::showReview() {
        auto planned = plan();
        if (!planned || planned->empty()) {
            outcome = planned ? "" : planned.error();
            return showOverview();
        }
        reviewing = true;
        changes   = std::move(*planned);
        outcome.clear();
        body->clearChildren();
        body->addChild(space(4));
        body->addChild(heading("Review: screenshots and clipboard", palette));
        body->addChild(paragraph("Applying writes the changes below, after keeping a copy so they can be undone. Hyprland reads the keys at once; a "
                                 "history turned on starts now too.",
                                 palette, true));
        setFooter(footer, {button("Cancel", 110, false, [this] { defer([this] { showOverview(); }); }), button("Apply", 110, true, [this] { defer([this] { apply(); }); })});
        diff.setRows(diffRows(changes, *palette));
        body->addChild(diff.element());
    }

    void ScreenshotsPage::apply() {
        if (!reviewing)
            return;
        const bool startHistory = historyWanted && !historyNow;
        const auto record       = configuration::applyChanges(changes, application::backupsFolder(foundation::currentEnvironment()), changeName);
        reviewing               = false;
        if (!record) {
            outcome = std::format("Nothing was written: {}.", record.error());
            return showOverview();
        }
        outcome = "Applied.";
        if (const auto hyprctl = program("hyprctl"))
            application::checkHyprlandConfig(loop, *hyprctl, *record, [this](std::optional<std::string> problem) {
                if (!problem)
                    return;
                outcome = *problem;
                if (!reviewing)
                    showOverview();
            });
        if (startHistory && program("cliphist") && desktop::findProcesses("wl-paste").empty())
            (void)desktop::startCommand(std::string{desktop::clipboardWatcher});
        refresh();
    }

    void ScreenshotsPage::undo() {
        const auto latest = configuration::latestChange(application::backupsFolder(foundation::currentEnvironment()), changeName);
        if (!latest)
            return showOverview();
        if (const auto undone = configuration::undoChange(*latest); !undone)
            outcome = std::format("Nothing was undone: {}.", undone.error());
        else
            outcome = "Undone.";
        refresh();
    }

}
