// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/essentials-page.hpp"

#include "application/config-check.hpp"
#include "application/lantern-folder.hpp"
#include "configuration/autostart.hpp"
#include "configuration/main-config.hpp"
#include "desktop/clipboard.hpp"
#include "desktop/process.hpp"
#include "desktop/system.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <format>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr auto changeName = "Essentials";

    }

    EssentialsPage::EssentialsPage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop) :
        palette{std::move(palette)},
        defer{std::move(defer)},
        loop{std::move(loop)},
        diff{this->palette, this->defer,
             {RowList::Column{.width = 24, .monospace = true, .secondary = false, .markup = true, .heading = ""},
              RowList::Column{.width = 0, .monospace = true, .secondary = false, .markup = true, .heading = ""}},
             20} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        root->addChild(pageTitle("Essentials", this->palette));
        root->addChild(paragraph("The programs a Hyprland desktop needs beside Hyprland, and whether this computer has them. Lantern installs nothing: each one "
                                 "missing shows the command that installs it, to run in a terminal.",
                                 this->palette));
        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        root->addChild(body);
        footer = leftRow();
        root->addChild(footer);
        refresh();
    }

    void EssentialsPage::refresh() {
        if (reviewing)
            return;
        found = desktop::essentials();
        showOverview();
    }

    void EssentialsPage::restyle() {
        if (!reviewing)
            showOverview();
    }

    const desktop::Essential* EssentialsPage::startable() const {
        const auto essential = std::ranges::find_if(found, [](const auto& essential) { return essential.start.has_value(); });
        return essential == found.end() ? nullptr : &*essential;
    }

    std::expected<std::optional<configuration::FileChange>, std::string> EssentialsPage::plan() const {
        const auto* essential = startable();
        if (!essential)
            return std::nullopt;
        const auto config = configuration::mainConfig(configuration::lanternLaunch());
        if (!config)
            return std::unexpected{config.error()};
        return configuration::planEdit(config->file, [lua = config->lua(), program = essential->start->string()](std::string_view text) {
            return configuration::withAutostart(text, lua, program, {});
        });
    }

    void EssentialsPage::showOverview() {
        reviewing = false;
        body->clearChildren();
        const auto environment  = foundation::currentEnvironment();
        const auto distribution = desktop::currentDistribution();
        const auto copier       = desktop::clipboardProgram(foundation::variable(environment, "PATH").value_or(""));
        // Two columns, each as tall as the page so that both start at the top.
        const auto columns = leftRow(0);
        columns->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->gap(32)->commence();
        const auto left  = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(18)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {0.48, 1.0}})->commence();
        const auto right = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(18)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {0.48, 1.0}})->commence();
        columns->addChild(left);
        columns->addChild(right);
        body->addChild(space(4));
        body->addChild(columns);
        for (std::size_t index = 0; index < found.size(); ++index)
            (index * 2 < found.size() ? left : right)->addChild(requirementBlock(found[index].requirement, distribution, copier, palette, defer, [this] {
                outcome.clear();
                refresh();
            }, found[index].problem));
        fillStatus();
    }

    void EssentialsPage::fillStatus() {
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> buttons;
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> away;
        if (configuration::latestChange(application::backupsFolder(foundation::currentEnvironment()), changeName))
            away.push_back(button("Undo last change", 160, false, [this] { defer([this] { undo(); }); }));
        if (startable())
            buttons.push_back(button("Start it now and at login", 230, true, [this] { defer([this] { showReview(); }); }));
        const auto missing = std::ranges::count_if(found, [](const auto& essential) { return !essential.requirement.met() || !essential.problem.empty(); });
        std::string note   = escapeMarkup(outcome);
        if (note.empty() && missing == 0)
            note = std::format("{}  {}", colored("✓", hex(palette->m_colors.accent)), "Everything is here.");
        else if (note.empty())
            note = escapeMarkup(std::format("{} of {} need{} your attention.", missing, found.size(), missing == 1 ? "s" : ""));
        const auto noteText = markupLine(note, palette);
        noteText->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        setFooter(footer, buttons, noteText, away);
    }

    void EssentialsPage::showReview() {
        const auto* essential = startable();
        auto        planned   = plan();
        if (!essential || !planned) {
            outcome = planned ? "" : std::format("Lantern cannot start it: {}.", planned.error());
            return showOverview();
        }
        reviewing = true;
        change    = std::move(*planned);
        outcome.clear();
        body->clearChildren();
        body->addChild(space(4));
        body->addChild(heading(std::format("Review: start {}", essential->start->filename().string()), palette));
        if (change) {
            body->addChild(paragraph(std::format("Applying starts {} now, and adds it to your Hyprland config's start, below, so that it runs after you log in "
                                                 "again. A copy of the config is kept so the change can be undone.",
                                                 essential->start->string()),
                                     palette, true));
            diff.setRows(diffRows({*change}, *palette));
            body->addChild(diff.element());
        } else
            body->addChild(paragraph(std::format("Your Hyprland config already starts {}, so it may have stopped since. Applying starts it again now.",
                                                 essential->start->string()),
                                     palette, true));
        setFooter(footer, {button("Cancel", 110, false, [this] { defer([this] { showOverview(); }); }), button("Apply", 110, true, [this] { defer([this] { apply(); }); })});
    }

    void EssentialsPage::apply() {
        const auto* essential = startable();
        if (!reviewing || !essential)
            return;
        const auto environment = foundation::currentEnvironment();
        if (change) {
            const auto record = configuration::applyChanges({*change}, application::backupsFolder(environment), changeName);
            if (!record) {
                outcome = std::format("Nothing was written: {}.", record.error());
                return showOverview();
            }
            if (const auto hyprctl = desktop::findProgram("hyprctl", foundation::variable(environment, "PATH").value_or("")))
                application::checkHyprlandConfig(loop, *hyprctl, *record, [this](std::optional<std::string> problem) {
                    if (!problem)
                        return;
                    outcome = *problem;
                    if (!reviewing)
                        showOverview();
                });
        }
        const auto started = desktop::startDetached(*essential->start);
        outcome            = started ? std::format("Started {}.", essential->start->filename().string()) : std::format("It did not start: {}.", started.error());
        reviewing          = false;
        // A moment for it to start, before checking that it runs.
        loop.after(std::chrono::milliseconds{1000}, [this] { refresh(); });
        showOverview();
    }

    void EssentialsPage::undo() {
        const auto latest = configuration::latestChange(application::backupsFolder(foundation::currentEnvironment()), changeName);
        if (!latest)
            return showOverview();
        if (const auto undone = configuration::undoChange(*latest); !undone)
            outcome = std::format("Nothing was undone: {}.", undone.error());
        else
            outcome = "Undone. What was started keeps running until you log out.";
        refresh();
    }

}
