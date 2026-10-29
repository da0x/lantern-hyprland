// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/event-log-page.hpp"

#include "application/event-description.hpp"
#include "ui/markup.hpp"
#include "ui/restyle.hpp"
#include "ui/texts.hpp"
#include "ui/widgets.hpp"
#include "ui/typing.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <sstream>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr double      rowHeight   = 30;
        constexpr double      timeWidth   = 110;
        constexpr double      nameWidth   = 230;
        constexpr double      searchWidth = 320;
        constexpr std::size_t kept        = 5000;

        std::string lower(std::string_view text) {
            std::string made{text};
            std::ranges::transform(made, made.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return made;
        }

        std::string now() {
            const auto time = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
            return std::format("{:%H:%M:%S}", std::chrono::floor<std::chrono::milliseconds>(time));
        }

    }

    EventLogPage::EventLogPage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer) :
        palette{std::move(palette)},
        defer{defer},
        list{this->palette, defer,
             {RowList::Column{.width = timeWidth, .monospace = true, .secondary = true, .markup = false, .heading = "When"},
              RowList::Column{.width = 0, .monospace = false, .secondary = false, .markup = true, .heading = "What happened"},
              RowList::Column{.width = nameWidth, .monospace = true, .secondary = true, .markup = false, .heading = "Event"}},
             rowHeight} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(8)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        title = pageTitle("Event log", this->palette);
        root->addChild(title);
        status = paragraph("Connecting to Hyprland.", this->palette);
        root->addChild(status);

        const auto toolbar = leftRow();
        search             = Hyprtoolkit::CTextboxBuilder::begin()
                     ->placeholder("Search events")
                     ->onTextEdited([this](CSharedPointer<Hyprtoolkit::CTextboxElement>, const std::string& text) {
                         query = text;
                         this->defer([this] { showRows(); });
                     })
                     ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {scaled(searchWidth), scaled(34)}})
                     ->commence();
        registerTextbox(search);
        toolbar->addChild(search);
        toolbar->addChild(space(1));
        pauseButton = button("Pause", 110, false, [this] {
            this->defer([this] {
                paused = !paused;
                unseen = 0;
                updateButtons();
                updateStatus();
                showRows();
            });
        });
        toolbar->addChild(pauseButton);
        everyButton = button("Show repeats", 150, false, [this] {
            this->defer([this] {
                every = !every;
                updateButtons();
                updateStatus();
                showRows();
            });
        });
        toolbar->addChild(everyButton);
        toolbar->addChild(button("Clear", 100, false, [this] {
            this->defer([this] {
                entries.clear();
                unseen = 0;
                updateStatus();
                showRows();
            });
        }));
        root->addChild(toolbar);

        const auto body = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        body->addChild(list.element());
        root->addChild(body);
        updateStatus();
    }

    void EventLogPage::showConnection(application::Connection connection, const std::string& reason) {
        this->connection = connection;
        connectionReason = reason;
        updateStatus();
    }

    void EventLogPage::learnWindows(const ipc::Tree& tree) {
        for (const auto& window : tree.windows)
            windows[window.address] = window.windowClass.empty() ? window.title : window.windowClass;
    }

    void EventLogPage::addEvents(const std::vector<ipc::Event>& events) {
        const auto named = [this](std::string_view address) -> std::optional<std::string> {
            const auto found = windows.find(std::string{address});
            return found != windows.end() ? std::optional{found->second} : std::nullopt;
        };
        const auto time = now();
        for (const auto& event : events) {
            // A window that opens is named from then on.
            if (event.name == "openwindow") {
                const auto first = event.data.find(',');
                const auto second = first == std::string::npos ? first : event.data.find(',', first + 1);
                const auto third  = second == std::string::npos ? second : event.data.find(',', second + 1);
                if (third != std::string::npos)
                    windows["0x" + event.data.substr(0, first)] = event.data.substr(second + 1, third - second - 1);
            }
            entries.push_front(Entry{.time = time, .event = event, .described = application::describeEvent(event, named)});
            if (paused)
                ++unseen;
        }
        while (entries.size() > kept)
            entries.pop_back();
        updateStatus();
        // Events come in bursts; the list is drawn once for each.
        if (!paused && !refreshPending) {
            refreshPending = true;
            defer([this] {
                refreshPending = false;
                showRows();
            });
        }
    }

    void EventLogPage::restyle() {
        showRows();
    }

    void EventLogPage::updateStatus() {
        std::string text;
        switch (connection) {
            case application::Connection::connecting: text = "Connecting to Hyprland."; break;
            case application::Connection::live:
                if (entries.empty()) {
                    text = "Listening to Hyprland. What it reports appears here as it happens, newest first.";
                } else {
                    const auto repeats = every ? 0 : std::ranges::count_if(entries, [](const Entry& entry) { return application::repeatsNewerEvent(entry.event.name); });
                    text = std::format("{} {} since Lantern started, newest first.", entries.size() - repeats, entries.size() - repeats == 1 ? "event" : "events");
                    if (repeats > 0)
                        text += std::format(" Hyprland also sent {} of them again in an older form; Show repeats lists those too.", repeats);
                }
                break;
            case application::Connection::lost:
                text = std::format("Not connected to Hyprland: {}. Trying again; events while it is away are missed.", connectionReason);
                break;
        }
        if (paused)
            text += unseen > 0 ? std::format(" Paused, with {} new kept for when you resume.", unseen) : " Paused.";
        status->setText(escapeMarkup(text));
    }

    void EventLogPage::updateButtons() {
        pauseButton->rebuild()->label(paused ? "Resume" : "Pause")->accent(paused)->commence();
        everyButton->rebuild()->accent(every)->commence();
    }

    void EventLogPage::showRows() {
        std::vector<std::string> words;
        std::stringstream        split{lower(query)};
        for (std::string word; split >> word;)
            words.push_back(word);
        std::vector<RowList::Row> rows;
        const auto                accent = hex(palette->m_colors.accent);
        for (const auto& entry : entries) {
            if (!every && application::repeatsNewerEvent(entry.event.name))
                continue;
            const auto haystack = lower(entry.described + " " + entry.event.name + " " + entry.event.data);
            if (!std::ranges::all_of(words, [&haystack](const std::string& word) { return haystack.contains(word); }))
                continue;
            // What changes the screen most stands out.
            const bool big = entry.event.name == "openwindow" || entry.event.name == "closewindow" || entry.event.name.starts_with("monitor") || entry.event.name == "configreloaded";
            rows.push_back(RowList::Row{
                .cells  = {entry.time, big ? colored(entry.described, accent) : escapeMarkup(entry.described), entry.event.name},
                .marked = false,
            });
        }
        list.setRows(std::move(rows));
    }

    void EventLogPage::embed() {
        root->removeChild(title);
        root->setMargin(0);
    }

}
