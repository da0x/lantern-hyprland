// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/autostart-page.hpp"

#include "application/command.hpp"
#include "application/config-check.hpp"
#include "application/lantern-folder.hpp"
#include "configuration/main-config.hpp"
#include "desktop/clipboard.hpp"
#include "desktop/process.hpp"
#include "desktop/system.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"
#include "ui/typing.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr auto changeName = "Autostart";

        std::optional<std::filesystem::path> program(std::string_view name) {
            return desktop::findProgram(name, foundation::variable(foundation::currentEnvironment(), "PATH").value_or(""));
        }

        std::string fileText(const std::filesystem::path& file) {
            std::ifstream     stream{file, std::ios::binary};
            std::stringstream text;
            text << stream.rdbuf();
            return text.str();
        }

        bool startsWithDex(const std::vector<configuration::StartedProgram>& started) {
            return std::ranges::any_of(started, [](const configuration::StartedProgram& one) { return one.on && (one.command == "dex" || one.command.starts_with("dex ")); });
        }

    }

    AutostartPage::AutostartPage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop) :
        palette{std::move(palette)},
        defer{std::move(defer)},
        loop{std::move(loop)},
        diff{this->palette, this->defer,
             {RowList::Column{.width = 24, .monospace = true, .secondary = false, .markup = true, .heading = ""},
              RowList::Column{.width = 0, .monospace = true, .secondary = false, .markup = true, .heading = ""}},
             20} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        root->addChild(pageTitle("Autostart", this->palette));
        root->addChild(paragraph("What starts when you log in. Hyprland starts the programs in its config; other programs ask to start with any desktop "
                                 "through autostart files. Lantern writes the changes after you review them, and they count from your next login.",
                                 this->palette));
        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        root->addChild(body);
        footer = leftRow();
        root->addChild(footer);

        adding = leftRow();
        typed  = Hyprtoolkit::CTextboxBuilder::begin()
                    ->placeholder("A command, such as nm-applet --indicator")
                    ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {scaled(340), scaled(34)}})
                    ->commence();
        registerTextbox(typed);
        adding->addChild(typed);
        adding->addChild(space(1));
        adding->addChild(button("Add", 90, false, [this] { this->defer([this] { add(std::string{typed->currentText()}); }); }));
        refresh();
    }

    void AutostartPage::refresh() {
        if (reviewing)
            return;
        const auto environment = foundation::currentEnvironment();
        const auto config      = configuration::mainConfig(configuration::lanternLaunch());
        configProblem          = config ? std::nullopt : std::optional{config.error()};
        startedNow             = config ? configuration::startedAtLogin(fileText(config->file), config->lua()) : std::vector<configuration::StartedProgram>{};
        startedWanted          = startedNow;
        const auto folders     = desktop::systemAutostartFolders(environment);
        entries = desktop::autostartEntries(desktop::userAutostartFolder(environment), folders, foundation::variable(environment, "XDG_CURRENT_DESKTOP").value_or("Hyprland"));
        entriesWanted.clear();
        showOverview();
        if (const auto systemctl = program("systemctl"))
            (void)application::runCommand(loop, *systemctl, {"--user", "is-active", "xdg-desktop-autostart.target"}, [this](bool, std::string output) {
                systemdStarts = output.starts_with("active");
                if (!reviewing)
                    showOverview();
            });
    }

    void AutostartPage::restyle() {
        if (!reviewing)
            showOverview();
    }

    void AutostartPage::add(std::string command) {
        while (!command.empty() && (command.back() == ' ' || command.back() == '\n'))
            command.pop_back();
        command.erase(0, command.find_first_not_of(' '));
        if (command.empty())
            return;
        typed->setText("");
        if (const auto found = std::ranges::find(startedWanted, command, &configuration::StartedProgram::command); found != startedWanted.end())
            found->on = true;
        else
            startedWanted.push_back({command, true});
        showOverview();
    }

    std::expected<std::vector<configuration::FileChange>, std::string> AutostartPage::plan() const {
        std::vector<configuration::FileChange> planned;
        if (startedWanted != startedNow) {
            const auto config = configuration::mainConfig(configuration::lanternLaunch());
            if (!config)
                return std::unexpected{config.error()};
            auto edited = configuration::planEdit(config->file, [this, lua = config->lua()](std::string_view text) {
                std::string result{text};
                for (const auto& was : startedNow) {
                    const auto wanted = std::ranges::find(startedWanted, was.command, &configuration::StartedProgram::command);
                    if (wanted == startedWanted.end())
                        result = configuration::withoutAutostart(result, lua, was.command);
                    else if (wanted->on != was.on)
                        result = configuration::withAutostartSwitched(result, lua, was.command, wanted->on);
                }
                for (const auto& wanted : startedWanted)
                    if (std::ranges::find(startedNow, wanted.command, &configuration::StartedProgram::command) == startedNow.end())
                        result = configuration::withAutostart(result, lua, wanted.command, {});
                return result;
            });
            if (!edited)
                return std::unexpected{edited.error()};
            if (*edited)
                planned.push_back(std::move(**edited));
        }
        const auto userFolder = desktop::userAutostartFolder(foundation::currentEnvironment());
        for (const auto& entry : entries) {
            const auto wanted = entriesWanted.find(entry.id);
            if (wanted == entriesWanted.end() || wanted->second == entry.on)
                continue;
            auto change = entry.users ? configuration::planEdit(entry.file, [on = wanted->second](std::string_view text) { return desktop::withEntryOn(text, on); })
                                      : configuration::planFile(userFolder / entry.id, desktop::withEntryOn(fileText(entry.file), wanted->second))
                                            .transform([](configuration::FileChange made) { return std::optional{std::move(made)}; });
            if (!change)
                return std::unexpected{change.error()};
            if (*change)
                planned.push_back(std::move(**change));
        }
        return planned;
    }

    void AutostartPage::showOverview() {
        reviewing = false;
        body->clearChildren();
        body->addChild(space(4));
        // Hyprland's on the left and the autostart files on the right, each column as tall
        // as the page, so that both start at the top.
        const auto columns = leftRow(0);
        columns->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->gap(32)->commence();
        const auto left  = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(8)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {0.48, 1.0}})->commence();
        const auto right = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(8)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {0.48, 1.0}})->commence();
        columns->addChild(left);
        columns->addChild(right);
        body->addChild(columns);
        fillHyprland(left);
        fillEntries(right);
        fillStatus();
    }

    void AutostartPage::fillHyprland(const CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column) {
        column->addChild(heading("Started by Hyprland", palette));
        if (configProblem) {
            column->addChild(paragraph(std::format("Lantern cannot find Hyprland's config: {}.", *configProblem), palette, true));
            return;
        }
        column->addChild(paragraph(startedWanted.empty() ? "Your config starts nothing at login." : "Off keeps the line in your config, commented out.", palette, true));
        for (std::size_t index = 0; index < startedWanted.size(); ++index) {
            const auto& one = startedWanted[index];
            const bool  added = std::ranges::find(startedNow, one.command, &configuration::StartedProgram::command) == startedNow.end();
            column->addChild(settingRow(one.command, added ? "added" : "", {"On", "Off", "Remove"}, one.on ? 0 : 1, palette,
                                        [this, command = one.command](std::size_t chosen) {
                                            const auto found = std::ranges::find(startedWanted, command, &configuration::StartedProgram::command);
                                            if (found == startedWanted.end())
                                                return;
                                            if (chosen == 2)
                                                startedWanted.erase(found);
                                            else
                                                found->on = chosen == 0;
                                            showOverview();
                                        },
                                        defer));
        }
        column->addChild(space(6));
        column->addChild(adding);
    }

    void AutostartPage::fillEntries(const CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column) {
        column->addChild(heading("Autostart files", palette));
        if (systemdStarts == true)
            column->addChild(paragraph("systemd starts the ones switched on, as uwsm sets it up to.", palette, true));
        else if (startsWithDex(startedWanted))
            column->addChild(paragraph("dex starts the ones switched on, from Hyprland's config.", palette, true));
        else if (systemdStarts == false) {
            column->addChild(paragraph("Nothing starts these yet: Hyprland does not read autostart files by itself. dex can, started from Hyprland's config.", palette));
            if (program("dex")) {
                const auto row = leftRow(scaled(40.0));
                row->addChild(button("Start them with dex", 200, false, [this] { defer([this] { add("dex -a"); }); }));
                column->addChild(row);
            } else
                column->addChild(requirementBlock(desktop::Requirement{.name     = "dex",
                                                                       .purpose  = "Starts the programs in autostart files when you log in.",
                                                                       .upstream = "https://github.com/jceb/dex",
                                                                       .package  = "dex",
                                                                       .found    = std::nullopt},
                                                  desktop::currentDistribution(),
                                                  desktop::clipboardProgram(foundation::variable(foundation::currentEnvironment(), "PATH").value_or("")), palette, defer, [this] { refresh(); }));
        }
        std::size_t others = 0;
        for (const auto& entry : entries) {
            if (!entry.forDesktop) {
                ++others;
                continue;
            }
            const auto wanted = entriesWanted.find(entry.id);
            const bool on     = wanted == entriesWanted.end() ? entry.on : wanted->second;
            column->addChild(settingRow(entry.name, entry.command, {"On", "Off"}, on ? 0 : 1, palette,
                                        [this, id = entry.id](std::size_t chosen) {
                                            entriesWanted[id] = chosen == 0;
                                            showOverview();
                                        },
                                        defer));
        }
        if (entries.size() == others)
            column->addChild(paragraph("There are none for Hyprland.", palette, true));
        if (others > 0)
            column->addChild(paragraph(std::format("{} more {} only for other desktops, such as KDE and GNOME.", others, others == 1 ? "is" : "are"), palette, true));
    }

    void AutostartPage::fillStatus() {
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

    void AutostartPage::showReview() {
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
        body->addChild(heading("Review: autostart", palette));
        body->addChild(paragraph("Applying writes the changes below, after keeping a copy of each file so they can be undone. Nothing is started or stopped "
                                 "now; the changes count from your next login.",
                                 palette, true));
        setFooter(footer, {button("Cancel", 110, false, [this] { defer([this] { showOverview(); }); }), button("Apply", 110, true, [this] { defer([this] { apply(); }); })});
        diff.setRows(diffRows(changes, *palette));
        body->addChild(diff.element());
    }

    void AutostartPage::apply() {
        if (!reviewing)
            return;
        const auto record = configuration::applyChanges(changes, application::backupsFolder(foundation::currentEnvironment()), changeName);
        reviewing         = false;
        if (!record) {
            outcome = std::format("Nothing was written: {}.", record.error());
            return showOverview();
        }
        outcome = "Applied. It counts from your next login.";
        if (const auto hyprctl = program("hyprctl"))
            application::checkHyprlandConfig(loop, *hyprctl, *record, [this](std::optional<std::string> problem) {
                if (!problem)
                    return;
                outcome = *problem;
                if (!reviewing)
                    showOverview();
            });
        refresh();
    }

    void AutostartPage::undo() {
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
