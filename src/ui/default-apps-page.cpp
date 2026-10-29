// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/default-apps-page.hpp"

#include "application/lantern-folder.hpp"
#include "desktop/clipboard.hpp"
#include "desktop/process.hpp"
#include "desktop/system.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <format>
#include <ranges>
#include <fstream>
#include <sstream>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr auto changeName = "Default apps";
        constexpr auto terminalKind = "Terminal";

        std::optional<std::filesystem::path> program(std::string_view name) {
            return desktop::findProgram(name, foundation::variable(foundation::currentEnvironment(), "PATH").value_or(""));
        }

        std::string fileText(const std::filesystem::path& file) {
            std::ifstream     stream{file, std::ios::binary};
            std::stringstream text;
            text << stream.rdbuf();
            return text.str();
        }

    }

    DefaultAppsPage::DefaultAppsPage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop) :
        palette{std::move(palette)},
        defer{std::move(defer)},
        loop{std::move(loop)},
        diff{this->palette, this->defer,
             {RowList::Column{.width = 24, .monospace = true, .secondary = false, .markup = true, .heading = ""},
              RowList::Column{.width = 0, .monospace = true, .secondary = false, .markup = true, .heading = ""}},
             20} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        root->addChild(pageTitle("Default apps", this->palette));
        root->addChild(paragraph("The app that opens each kind of link and file, among the apps installed that say they can. While none is chosen, the "
                                 "system picks one, which can change as apps are installed. Lantern writes the changes after you review them.",
                                 this->palette));
        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        root->addChild(body);
        footer = leftRow();
        root->addChild(footer);
        refresh();
    }

    void DefaultAppsPage::refresh() {
        if (reviewing)
            return;
        const auto environment = foundation::currentEnvironment();
        installed              = desktop::applications(desktop::applicationFolders(environment));
        std::vector<std::string> lists;
        for (const auto& file : desktop::mimeappsFiles(environment))
            lists.push_back(fileText(file));
        std::vector<std::string> caches;
        for (const auto& folder : desktop::applicationFolders(environment))
            caches.push_back(fileText(folder / "mimeinfo.cache"));
        now.clear();
        fallback.clear();
        for (const auto& kind : desktop::defaultKinds()) {
            now[kind.name]      = desktop::defaultFor(lists, kind.types.front(), installed);
            fallback[kind.name] = desktop::fallbackFor(lists, caches, kind.types.front(), installed);
        }
        wanted      = now;
        terminalNow = desktop::preferredTerminal(fileText(desktop::terminalsFile(environment)));
        if (terminalNow && !std::ranges::contains(installed, *terminalNow, &desktop::Application::id))
            terminalNow.reset();
        terminalWanted = terminalNow;
        showOverview();
    }

    void DefaultAppsPage::restyle() {
        if (!reviewing)
            showOverview();
    }

    std::expected<std::vector<configuration::FileChange>, std::string> DefaultAppsPage::plan() const {
        std::vector<configuration::FileChange> planned;
        const auto                             environment = foundation::currentEnvironment();
        if (wanted != now) {
            auto list = configuration::planEdit(desktop::userMimeappsFile(environment), [this](std::string_view text) {
                std::string result{text};
                for (const auto& kind : desktop::defaultKinds()) {
                    const auto& id = wanted.at(kind.name);
                    if (!id || id == now.at(kind.name))
                        continue;
                    // Only the types the app says it opens, and always the one shown.
                    const auto                app = std::ranges::find(installed, *id, &desktop::Application::id);
                    std::vector<std::string> types{kind.types.front()};
                    for (const auto& type : kind.types | std::views::drop(1))
                        if (app != installed.end() && std::ranges::contains(app->types, type))
                            types.push_back(type);
                    result = desktop::withDefault(result, types, *id);
                }
                return result;
            });
            if (!list)
                return std::unexpected{list.error()};
            if (*list)
                planned.push_back(std::move(**list));
        }
        if (terminalWanted && terminalWanted != terminalNow) {
            auto list = configuration::planEdit(desktop::terminalsFile(environment), [this](std::string_view text) { return desktop::withTerminal(text, *terminalWanted); });
            if (!list)
                return std::unexpected{list.error()};
            if (*list)
                planned.push_back(std::move(**list));
        }
        return planned;
    }

    void DefaultAppsPage::showOverview() {
        reviewing = false;
        body->clearChildren();
        body->addChild(space(4));
        // Links, folders and text on the left with the terminal, and media on the right,
        // each column as tall as the page, so that both start at the top.
        const auto columns = leftRow(0);
        columns->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->gap(32)->commence();
        const auto left  = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(8)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {0.48, 1.0}})->commence();
        const auto right = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(8)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {0.48, 1.0}})->commence();
        columns->addChild(left);
        columns->addChild(right);
        body->addChild(columns);
        const auto kinds = desktop::defaultKinds();
        for (std::size_t index = 0; index < kinds.size(); ++index)
            addKind(index < 4 ? left : right, kinds[index]);
        addTerminal(left);
        fillStatus();
    }

    void DefaultAppsPage::addKind(const CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column, const desktop::DefaultKind& kind) {
        std::vector<std::string> ids;
        for (const auto& app : installed)
            if (std::ranges::any_of(kind.types, [&](const std::string& type) { return std::ranges::contains(app.types, type); }))
                ids.push_back(app.id);
        const auto& chosen = wanted.at(kind.name);
        if (chosen && !std::ranges::contains(ids, *chosen))
            ids.push_back(*chosen);
        if (ids.empty()) {
            column->addChild(settingRow(kind.name, kind.about, {"No app installed opens these"}, 0, palette, [](std::size_t) {}, defer));
            return;
        }
        const auto nameOf = [this](const std::string& id) {
            const auto app = std::ranges::find(installed, id, &desktop::Application::id);
            return app == installed.end() ? id : app->name;
        };
        std::vector<std::string> items;
        if (!chosen) {
            const auto& meanwhile = fallback.at(kind.name);
            items.push_back(meanwhile ? std::format("Not chosen, {} for now", nameOf(*meanwhile)) : "Not chosen");
        }
        for (const auto& id : ids)
            items.push_back(nameOf(id));
        const auto current = chosen ? static_cast<std::size_t>(std::ranges::find(ids, *chosen) - ids.begin()) : 0;
        column->addChild(settingRow(kind.name, kind.about, std::move(items), current, palette,
                                    [this, name = kind.name, ids, offset = chosen ? 0U : 1U](std::size_t picked) {
                                        if (picked >= offset && picked - offset < ids.size())
                                            wanted[name] = ids[picked - offset];
                                        showOverview();
                                    },
                                    defer));
    }

    void DefaultAppsPage::addTerminal(const CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column) {
        column->addChild(space(10));
        if (!program("xdg-terminal-exec")) {
            column->addChild(requirementBlock(desktop::Requirement{.name     = "Default terminal",
                                                                   .purpose  = "xdg-terminal-exec, which opens apps that run in a terminal, such as vim from the app "
                                                                               "launcher, in the terminal you choose here.",
                                                                   .upstream = "https://github.com/Vladimir-csp/xdg-terminal-exec",
                                                                   .package  = "xdg-terminal-exec",
                                                                   .found    = std::nullopt},
                                              desktop::currentDistribution(),
                                              desktop::clipboardProgram(foundation::variable(foundation::currentEnvironment(), "PATH").value_or("")), palette, defer, [this] { refresh(); }));
            return;
        }
        std::vector<std::string> ids;
        std::vector<std::string> items;
        if (!terminalWanted)
            items.emplace_back("Not chosen");
        for (const auto& app : installed)
            if (app.terminal) {
                ids.push_back(app.id);
                items.push_back(app.name);
            }
        const auto current = terminalWanted ? static_cast<std::size_t>(std::ranges::find(ids, *terminalWanted) - ids.begin()) : 0;
        column->addChild(settingRow(terminalKind, "opens apps that run in a terminal", std::move(items), current, palette,
                                    [this, ids, offset = terminalWanted ? 0U : 1U](std::size_t picked) {
                                        if (picked >= offset && picked - offset < ids.size())
                                            terminalWanted = ids[picked - offset];
                                        showOverview();
                                    },
                                    defer));
    }

    void DefaultAppsPage::fillStatus() {
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

    void DefaultAppsPage::showReview() {
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
        body->addChild(heading("Review: default apps", palette));
        body->addChild(paragraph("Applying writes the changes below, after keeping a copy of each file so they can be undone. Apps read them the next "
                                 "time they open a link or a file.",
                                 palette, true));
        setFooter(footer, {button("Cancel", 110, false, [this] { defer([this] { showOverview(); }); }), button("Apply", 110, true, [this] { defer([this] { apply(); }); })});
        diff.setRows(diffRows(changes, *palette));
        body->addChild(diff.element());
    }

    void DefaultAppsPage::apply() {
        if (!reviewing)
            return;
        const auto record = configuration::applyChanges(changes, application::backupsFolder(foundation::currentEnvironment()), changeName);
        reviewing         = false;
        outcome           = record ? "Applied." : std::format("Nothing was written: {}.", record.error());
        refresh();
    }

    void DefaultAppsPage::undo() {
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
