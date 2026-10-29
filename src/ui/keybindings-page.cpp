// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/keybindings-page.hpp"

#include "ui/markup.hpp"
#include "ui/restyle.hpp"
#include "ui/texts.hpp"
#include "ui/typing.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <format>
#include <sstream>
#include <string_view>
#include <utility>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprtoolkit::CFontSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr double rowHeight    = 30;
        constexpr double keysWidth    = 300;
        constexpr double notesWidth   = 240;
        constexpr double buttonHeight = 30;
        constexpr double searchWidth  = 280;

        std::string lower(std::string_view text) {
            std::string made{text};
            std::ranges::transform(made, made.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return made;
        }

        // Hyprland names mouse buttons by their Linux input codes.
        std::string keyName(const std::string& key) {
            constexpr std::array names{
                std::pair{std::string_view{"mouse:272"}, std::string_view{"left button"}},
                std::pair{std::string_view{"mouse:273"}, std::string_view{"right button"}},
                std::pair{std::string_view{"mouse:274"}, std::string_view{"middle button"}},
                std::pair{std::string_view{"mouse_down"}, std::string_view{"wheel down"}},
                std::pair{std::string_view{"mouse_up"}, std::string_view{"wheel up"}},
            };
            const auto found = std::ranges::find(names, std::string_view{key}, &std::pair<std::string_view, std::string_view>::first);
            return found != names.end() ? std::string{found->second} : key;
        }

        // What the binding does in words, when it says. Otherwise entry() shows its action.
        std::string described(const application::KeybindingRow& row) {
            if (!row.live.description.empty())
                return row.live.description;
            if (row.recorded)
                return row.recorded->description;
            // Under Lua, Hyprland reports no action at all, only an internal number.
            if (row.live.fromLua())
                return {};
            return row.live.argument.empty() ? row.live.dispatcher : std::format("{} {}", row.live.dispatcher, row.live.argument);
        }

        // Code in the monospace font, as one cell.
        std::string monospace(const std::string& markup, const Hyprtoolkit::CPalette& palette) {
            return std::format("<span font_family=\"{}\">{}</span>", escapeMarkup(palette.m_vars.fontFamilyMonospace), markup);
        }

        struct Flags {
            std::string_view submap;
            bool             release      = false;
            bool             repeat       = false;
            bool             longPress    = false;
            bool             locked       = false;
            bool             nonConsuming = false;
            bool             inactive     = false;
        };

        // Where the binding was set, in the link color since clicking opens it, then
        // anything unusual about it.
        std::string notes(const std::optional<configuration::Location>& location, const Flags& flags, const Hyprtoolkit::CPalette& palette) {
            std::vector<std::string> extras;
            const auto               add = [&extras](bool set, std::string text) {
                if (set)
                    extras.push_back(std::move(text));
            };
            add(flags.inactive, "not active");
            add(!flags.submap.empty(), std::format("submap {}", flags.submap));
            add(flags.release, "on release");
            add(flags.repeat, "repeats");
            add(flags.longPress, "long press");
            add(flags.locked, "also when locked");
            add(flags.nonConsuming, "passes the key on");

            std::string text = location ? colored(location->brief(), hex(palette.m_colors.linkText)) : std::string{};
            for (const auto& extra : extras)
                text += (text.empty() ? "" : "  ·  ") + escapeMarkup(extra);
            return text;
        }

        // The path the way a person would write it: under their home folder, starting with ~.
        std::string shortPath(const std::filesystem::path& path) {
            const char* home = std::getenv("HOME");
            if (home == nullptr || *home == '\0')
                return path.string();
            const auto relative = path.lexically_relative(home);
            if (relative.empty() || relative.native().starts_with(".."))
                return path.string();
            return (std::filesystem::path{"~"} / relative).string();
        }

        CSharedPointer<Hyprtoolkit::CButtonElement> button(std::string label, double width, std::function<void()> clicked) {
            const auto made = Hyprtoolkit::CButtonBuilder::begin()
                ->label(std::move(label))
                ->noBorder(true)
                ->size(CDynamicSize{width > 0 ? CDynamicSize::HT_SIZE_ABSOLUTE : CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_ABSOLUTE, {width > 0 ? width : 1.0, buttonHeight}})
                ->onMainClick([clicked = std::move(clicked)](CSharedPointer<Hyprtoolkit::CButtonElement>) { clicked(); })
                ->commence();
            restyled(made);
            return made;
        }

    }

    KeybindingsPage::KeybindingsPage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer) :
        palette{std::move(palette)},
        defer{defer},
        list{this->palette, defer,
             {RowList::Column{.width = keysWidth, .monospace = false, .secondary = false, .markup = true, .heading = "Keys"},
              RowList::Column{.width = 0, .monospace = false, .secondary = false, .markup = true, .heading = "Does"},
              RowList::Column{.width = notesWidth, .monospace = false, .secondary = true, .markup = true, .heading = "Set at"}},
             rowHeight},
        sourceView{this->palette, defer} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);

        root->addChild(pageTitle("Keyboard shortcuts", this->palette));
        status = paragraph("Connecting to Hyprland.", this->palette);
        root->addChild(status);
        luaNote = paragraph({}, this->palette, true);
        root->addChild(luaNote);

        // A two-way switch, then one tab per config file while the source is showing.
        const auto toolbar = Hyprtoolkit::CRowLayoutBuilder::begin()->gap(4)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, buttonHeight}})->commence();
        listButton         = button("List", 100, [this] { showList(); });
        sourceButton       = button("Source", 100, [this] {
            if (!openFile && !sources.empty())
                showSource(sources.front().path, 0, true);
        });
        fileButtons = Hyprtoolkit::CRowLayoutBuilder::begin()->gap(4)->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, buttonHeight}})->commence();
        fileButtons->setGrow(true, false);
        search = Hyprtoolkit::CTextboxBuilder::begin()
                     ->placeholder("Search keys, actions and files")
                     ->onTextEdited([this](CSharedPointer<Hyprtoolkit::CTextboxElement>, const std::string& text) {
                         query = text;
                         this->defer([this] {
                             if (openFile)
                                 showList();
                             showEntries();
                         });
                     })
                     ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {searchWidth, buttonHeight}})
                     ->commence();
        registerTextbox(search);
        clashButton = button("Clashes", 150, [this] {
            this->defer([this] {
                onlyClashes = !onlyClashes;
                if (openFile)
                    showList();
                updateClashButton();
                showEntries();
            });
        });
        toolbar->addChild(listButton);
        toolbar->addChild(sourceButton);
        toolbar->addChild(Hyprtoolkit::CNullBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {20.0, 1.0}})->commence());
        toolbar->addChild(search);
        toolbar->addChild(clashButton);
        toolbar->addChild(Hyprtoolkit::CNullBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {20.0, 1.0}})->commence());
        toolbar->addChild(fileButtons);
        root->addChild(Hyprtoolkit::CNullBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 8.0}})->commence());
        root->addChild(toolbar);

        caption = paragraph({}, this->palette, true);

        // Holds the list or the source; its height is what is left.
        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        root->addChild(Hyprtoolkit::CNullBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 6.0}})->commence());
        root->addChild(body);

        list.setClicked([this](std::size_t row) {
            if (row >= targets.size())
                return;
            const auto target = targets[row];
            if (target.toggles) {
                const auto& identity = entries[*target.toggles].identity;
                if (!expanded.erase(identity))
                    expanded.insert(identity);
                showEntries();
            } else if (target.opens) {
                showSource(target.opens->file, target.opens->line, true);
            }
        });
        list.setRows({RowList::Row{.cells = {{}, escapeMarkup("Reading your keybindings."), {}}, .marked = false}});
        showList();
    }

    void KeybindingsPage::updateClashButton() {
        clashButton->rebuild()->label(clashing == 0 ? std::string{"Clashes"} : std::format("Clashes ({})", clashing))->accent(onlyClashes)->commence();
    }

    void KeybindingsPage::updateSwitch() {
        // Rebuilt rather than changed, the only way hyprtoolkit restyles a button.
        listButton->rebuild()->accent(!openFile)->commence();
        sourceButton->rebuild()->accent(openFile.has_value())->commence();
    }

    void KeybindingsPage::showList() {
        openFile.reset();
        markedLine = 0;
        body->clearChildren();
        body->addChild(list.element());
        updateSwitch();
        updateFileButtons();
    }

    const application::SourceFile* KeybindingsPage::source(const std::filesystem::path& file) const {
        const auto found = std::ranges::find(sources, file, &application::SourceFile::path);
        return found != sources.end() ? &*found : nullptr;
    }

    void KeybindingsPage::showSource(const std::filesystem::path& file, int line, bool jump) {
        const auto* shown = source(file);
        if (shown == nullptr)
            return;
        const bool wasList = !openFile;
        openFile           = file;
        markedLine         = line;
        sourceView.show(*shown, line, jump);
        caption->setText(escapeMarkup(line > 0 ? std::format("{}, line {}. As it is on disk now, which Hyprland may not have reloaded yet.", shortPath(file), line)
                                               : std::format("{}. As it is on disk now, which Hyprland may not have reloaded yet.", shortPath(file))));
        if (wasList) {
            body->clearChildren();
            body->addChild(caption);
            body->addChild(sourceView.element());
            updateSwitch();
        }
        updateFileButtons();
    }

    void KeybindingsPage::updateFileButtons() {
        fileButtons->clearChildren();
        if (!openFile || sources.size() < 2)
            return;
        const auto folder = sources.front().path.parent_path();
        for (const auto& file : sources) {
            auto tab = button(file.path.lexically_relative(folder).string(), 0, [this, path = file.path] {
                // Showing a file replaces this button, so not from inside its handler.
                defer([this, path] { showSource(path, 0, true); });
            });
            if (file.path == *openFile)
                tab->rebuild()->accent(true)->commence();
            fileButtons->addChild(tab);
        }
    }

    void KeybindingsPage::showConnection(application::Connection connection, const std::string& reason) {
        this->connection = connection;
        connectionReason = reason;
        updateStatus();
    }

    void KeybindingsPage::restyle() {
        if (shown) {
            const auto again = *shown;
            showKeybindings(again);
        }
    }

    void KeybindingsPage::showKeybindings(const std::expected<application::Keybindings, std::string>& keybindings) {
        shown = keybindings;
        if (!keybindings) {
            bindingsError = keybindings.error();
            updateStatus();
            return;
        }
        bindingsError.clear();
        configurationProblem = keybindings->configurationProblem;
        configurationDoubts  = keybindings->configurationDoubts;
        count                = keybindings->rows.size();
        inactive             = keybindings->inactive.size();

        const auto unexplained = std::ranges::count_if(keybindings->rows, [](const auto& row) { return row.live.fromLua() && !row.recorded && row.live.description.empty(); });
        luaNote->setText(unexplained == 0 ? std::string{" "}
                                          : std::format("Hyprland does not report what a Lua binding does, and {} of these could not be found in your config.", unexplained));

        // The sources first: a function's lines come from them.
        sources = keybindings->sources;

        // Which other bindings share each one's keys: where they were set, when known.
        const auto                            groups = application::clashes(keybindings->rows);
        std::vector<std::vector<std::size_t>> sharing(keybindings->rows.size());
        for (const auto& group : groups)
            for (const auto index : group)
                for (const auto other : group)
                    if (other != index)
                        sharing[index].push_back(other);
        clashing = groups.size();

        entries.clear();
        for (std::size_t index = 0; index < keybindings->rows.size(); ++index) {
            const auto& row      = keybindings->rows[index];
            const auto& live     = row.live;
            const auto  location = row.recorded ? std::optional{row.recorded->location} : std::nullopt;
            const auto  keys     = live.modifiers.with(keyName(live.key));
            std::string clash;
            if (!sharing[index].empty()) {
                std::vector<std::string> places;
                for (const auto other : sharing[index])
                    if (keybindings->rows[other].recorded)
                        places.push_back(keybindings->rows[other].recorded->location.brief());
                clash = places.size() == sharing[index].size() ? std::format("same keys as {}", places.front()) : std::format("bound {} times", sharing[index].size() + 1);
                if (places.size() == sharing[index].size())
                    for (std::size_t more = 1; more < places.size(); ++more)
                        clash += std::format(", {}", places[more]);
            }
            auto noted = notes(location,
                               Flags{.submap = live.submap, .release = live.release, .repeat = live.repeat, .longPress = live.longPress, .locked = live.locked, .nonConsuming = live.nonConsuming},
                               *palette);
            if (!clash.empty())
                noted = colored(clash, hex(palette->m_colors.accent)) + (noted.empty() ? "" : "  ·  ") + noted;
            auto made    = entry(keyCaps(keys, *palette), row.recorded ? row.recorded->action : configuration::Value{}, described(row), location, noted,
                                 std::format("{} {} {} {} {}", keys, described(row), location ? location->brief() : std::string{}, live.submap, clash));
            made.clashes = !clash.empty();
            entries.push_back(std::move(made));
        }
        for (const auto& binding : keybindings->inactive) {
            const auto keys = binding.modifiers.with(keyName(binding.key));
            entries.push_back(entry(keyCaps(keys, *palette), binding.action, binding.description, binding.location,
                                    notes(binding.location,
                                          Flags{.submap       = binding.submap,
                                                .release      = binding.release,
                                                .repeat       = binding.repeat,
                                                .longPress    = binding.longPress,
                                                .locked       = binding.locked,
                                                .nonConsuming = binding.nonConsuming,
                                                .inactive     = true},
                                          *palette),
                                    std::format("{} {} {} {} not active", keys, binding.description, binding.location.brief(), binding.submap)));
        }
        updateClashButton();
        showEntries();

        // The source view follows the files on disk, staying where it was.
        if (openFile) {
            if (source(*openFile) != nullptr)
                showSource(*openFile, markedLine, false);
            else
                showList();
        }
        updateStatus();
    }

    KeybindingsPage::Entry KeybindingsPage::entry(std::string keys, const configuration::Value& action, std::string described,
                                                  const std::optional<configuration::Location>& location, std::string notes, std::string searchable) const {
        Entry made{.identity       = keys + (location ? location->brief() : std::string{}),
                   .keys           = std::move(keys),
                   .action         = {},
                   .notes          = std::move(notes),
                   .location       = location,
                   .body           = {},
                   .expandedAction = {},
                   .searchable     = lower(searchable),
                   .clashes        = false};
        if (!described.empty()) {
            made.action = escapeMarkup(described);
            return made;
        }

        // A function: its own text from the file, when Lantern can find it.
        const auto*                     function = action.function();
        const application::SourceFile* file     = function != nullptr ? source(function->definition.file) : nullptr;
        const auto                      range    = file != nullptr ? syntax::luaFunctionAt(file->text, function->definition.line) : std::nullopt;
        if (!range) {
            made.action = action.data.index() == 0 ? std::string{} : luaMarkup(action.lua(), *palette);
            if (action.data.index() != 0)
                made.searchable += " " + lower(action.lua());
            return made;
        }

        const std::string_view text = file->text;
        made.searchable += " " + lower(text.substr(range->begin, range->end - range->begin));
        auto                   span = file->spans.cbegin();
        std::size_t            begin = range->begin;
        int                    line  = function->definition.line;
        std::vector<std::string> lines;
        while (begin < range->end) {
            const auto end = std::min(text.find('\n', begin) == std::string_view::npos ? text.size() : text.find('\n', begin), range->end);
            lines.push_back(codeMarkup(text, begin, end, file->spans, span, *palette));
            if (lines.size() > 1)
                made.body.emplace_back(monospace("  " + lines.back(), *palette), configuration::Location{.file = function->definition.file, .line = line});
            begin = end + 1;
            ++line;
        }
        const auto arrow  = colored(lines.size() > 1 ? "▶ " : "", hex(palette->m_colors.accent));
        const auto folded = lines.size() > 1 ? colored(std::format("  … {} lines", lines.size()), hex(palette->m_colors.text.mix(palette->m_colors.base, 0.5F))) : std::string{};
        made.action         = monospace(arrow + lines.front(), *palette) + folded;
        made.expandedAction = monospace(colored("▼ ", hex(palette->m_colors.accent)) + lines.front(), *palette);
        return made;
    }

    void KeybindingsPage::showEntries() {
        std::vector<std::string> words;
        std::istringstream       split{lower(query)};
        for (std::string word; split >> word;)
            words.push_back(word);
        std::vector<RowList::Row> rows;
        targets.clear();
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const auto& shown = entries[index];
            if (onlyClashes && !shown.clashes)
                continue;
            if (!std::ranges::all_of(words, [&shown](const std::string& word) { return shown.searchable.contains(word); }))
                continue;
            const bool open = !shown.body.empty() && expanded.contains(shown.identity);
            rows.push_back(RowList::Row{.cells = {shown.keys, open ? shown.expandedAction : shown.action, shown.notes}, .marked = shown.clashes});
            targets.push_back(shown.body.empty() ? Target{.toggles = std::nullopt, .opens = shown.location} : Target{.toggles = index, .opens = std::nullopt});
            if (!open)
                continue;
            for (const auto& [markup, location] : shown.body) {
                rows.push_back(RowList::Row{.cells = {{}, markup, escapeMarkup(location.brief())}, .marked = false});
                targets.push_back(Target{.toggles = std::nullopt, .opens = location});
            }
        }
        if (rows.empty() && !entries.empty())
            rows.push_back(RowList::Row{.cells = {{}, escapeMarkup(onlyClashes && clashing == 0 ? "No keys are bound twice." : "No binding matches."), {}}, .marked = false});
        list.setRows(std::move(rows));
    }

    void KeybindingsPage::updateStatus() {
        std::string text;
        switch (connection) {
            case application::Connection::connecting: text = "Connecting to Hyprland."; break;
            case application::Connection::live: text = std::format("{} bindings, live from Hyprland.", count); break;
            case application::Connection::lost:
                text = std::format("Not connected to Hyprland: {}. Trying again; the list below may be out of date.", connectionReason);
                break;
        }
        if (inactive != 0)
            text += std::format(" {} more in your config are not active.", inactive);
        if (clashing != 0)
            text += std::format(" {} {} bound more than once, marked in the accent.", clashing, clashing == 1 ? "key is" : "keys are");
        if (!configurationProblem.empty())
            text += std::format(" Your config: {}.", configurationProblem);
        for (const auto& doubt : configurationDoubts)
            text += std::format(" {}", doubt);
        if (!bindingsError.empty())
            text += std::format(" Could not read the bindings: {}.", bindingsError);
        status->setText(escapeMarkup(text));
    }

}
