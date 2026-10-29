// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/options-page.hpp"

#include "ui/markup.hpp"
#include "ui/restyle.hpp"
#include "ui/texts.hpp"
#include "ui/widgets.hpp"
#include "ui/typing.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>
#include <sstream>
#include <tuple>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprtoolkit::CFontSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr double rowHeight   = 30;
        constexpr double nameWidth   = 330;
        constexpr double valueWidth  = 260;
        constexpr double searchWidth = 320;

        std::string lower(std::string_view text) {
            std::string made{text};
            std::ranges::transform(made, made.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return made;
        }

        // Every word of the search is in the option's name or what it does.
        bool matches(const application::OptionRow& row, const std::string& query) {
            const auto        haystack = lower(row.description.name + " " + row.description.description);
            std::stringstream words{lower(query)};
            std::string       word;
            while (words >> word)
                if (!haystack.contains(word))
                    return false;
            return true;
        }

        // The name with the sections above it faded, so the option's own name stands out.
        std::string nameMarkup(const std::string& name) {
            const auto last = name.find_last_of(':');
            if (last == std::string::npos)
                return escapeMarkup(name);
            return std::format("<span alpha=\"55%\">{}</span>{}", escapeMarkup(name.substr(0, last + 1)), escapeMarkup(name.substr(last + 1)));
        }

        // A color Hyprland writes as aarrggbb, as a swatch in that color.
        std::optional<std::string> swatch(std::string_view word) {
            if (word.size() != 8 || !std::ranges::all_of(word, [](unsigned char c) { return std::isxdigit(c); }))
                return std::nullopt;
            return std::format("<span foreground=\"#{}\">■</span>", word.substr(2));
        }

        // A value as it reads best: a color with its swatch, a number with the name it
        // stands for, an empty one as such.
        std::string valueMarkup(const std::string& value, const ipc::OptionDescription& description) {
            if (value.empty())
                return "<span alpha=\"55%\">empty</span>";
            std::string swatches;
            for (const auto& word : value | std::views::split(' '))
                if (const auto shown = swatch(std::string_view{word}))
                    swatches += *shown;
            auto text = escapeMarkup(value);
            for (const auto& [choice, number] : description.choices)
                if (value == std::to_string(number))
                    text += std::format(" <span alpha=\"65%\">· {}</span>", escapeMarkup(choice));
            return swatches.empty() ? text : swatches + " " + text;
        }

    }

    OptionsPage::OptionsPage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer) :
        palette{std::move(palette)},
        defer{defer},
        list{this->palette, defer,
             {RowList::Column{.width = nameWidth, .monospace = true, .secondary = false, .markup = true, .heading = "Option"},
              RowList::Column{.width = valueWidth, .monospace = true, .secondary = false, .markup = true, .heading = "Value"},
              RowList::Column{.width = 0, .monospace = false, .secondary = true, .markup = true, .heading = "What it does"}},
             rowHeight} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(8)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        root->addChild(pageTitle("Options", this->palette));
        status = paragraph("Connecting to Hyprland.", this->palette);
        root->addChild(status);

        const auto toolbar = leftRow();
        search             = Hyprtoolkit::CTextboxBuilder::begin()
                     ->placeholder("Search options")
                     ->onTextEdited([this](CSharedPointer<Hyprtoolkit::CTextboxElement>, const std::string& text) {
                         query = text;
                         this->defer([this] { showRows(); });
                     })
                     ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {scaled(searchWidth), scaled(34)}})
                     ->commence();
        registerTextbox(search);
        toolbar->addChild(search);
        toolbar->addChild(space(1));
        // Wide enough for their counts, which come later: a button's width is fixed.
        for (const auto& [which, label, width] : {std::tuple{Filter::everything, "Everything", 150.0}, std::tuple{Filter::set, "Set in your config", 210.0},
                                                  std::tuple{Filter::changed, "Changed from default", 230.0}}) {
            filterButtons.emplace_back(which, button(label, width, which == filter, [this, which] {
                                           this->defer([this, which] {
                                               filter = which;
                                               updateFilters();
                                               showRows();
                                           });
                                       }));
            toolbar->addChild(filterButtons.back().second);
        }
        root->addChild(toolbar);

        const auto body = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        body->addChild(list.element());
        root->addChild(body);
        list.setClicked([this](std::size_t index) {
            if (index >= shownRows.size())
                return;
            selected = rows[shownRows[index]].description.name;
            this->defer([this] {
                showRows();
                showDetails();
            });
        });

        details = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(4)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        root->addChild(details);
        showDetails();
    }

    void OptionsPage::showConnection(application::Connection connection, const std::string& reason) {
        this->connection = connection;
        connectionReason = reason;
        updateStatus();
    }

    void OptionsPage::showOptions(const std::expected<application::Options, std::string>& options) {
        if (!options) {
            optionsError = options.error();
            updateStatus();
            return;
        }
        optionsError.clear();
        received             = true;
        rows                 = options->rows;
        configurationProblem = options->configurationProblem;
        updateStatus();
        updateFilters();
        showRows();
        showDetails();
    }

    void OptionsPage::restyle() {
        showRows();
        showDetails();
    }

    void OptionsPage::updateStatus() {
        std::string text;
        const auto  set     = std::ranges::count_if(rows, &application::OptionRow::set);
        const auto  changed = std::ranges::count_if(rows, &application::OptionRow::changed);
        switch (connection) {
            case application::Connection::connecting: text = "Connecting to Hyprland."; break;
            case application::Connection::live:
                text = received ? std::format("{} options, live from Hyprland. Your config sets {}, and {} of them differ from the default. Click one to see more about it.",
                                              rows.size(), set, changed)
                                : "Reading the options from Hyprland.";
                break;
            case application::Connection::lost:
                text = std::format("Not connected to Hyprland: {}. Trying again; the list below may be out of date.", connectionReason);
                break;
        }
        if (!configurationProblem.empty())
            text += std::format(" Where each was set is unknown: {}.", configurationProblem);
        if (!optionsError.empty())
            text += std::format(" Could not read the options: {}.", optionsError);
        status->setText(escapeMarkup(text));
    }

    void OptionsPage::updateFilters() {
        const auto count = [this](Filter which) {
            switch (which) {
                case Filter::everything: return static_cast<long>(rows.size());
                case Filter::set: return static_cast<long>(std::ranges::count_if(rows, &application::OptionRow::set));
                case Filter::changed: return static_cast<long>(std::ranges::count_if(rows, &application::OptionRow::changed));
            }
            return 0L;
        };
        for (const auto& [which, made] : filterButtons) {
            const auto* label = which == Filter::everything ? "Everything" : which == Filter::set ? "Set in your config" : "Changed from default";
            made->rebuild()->label(received ? std::format("{} · {}", label, count(which)) : std::string{label})->accent(which == filter)->commence();
        }
    }

    void OptionsPage::showRows() {
        shownRows.clear();
        std::vector<RowList::Row> made;
        const auto                accent = hex(palette->m_colors.accent);
        for (std::size_t index = 0; index < rows.size(); ++index) {
            const auto& row = rows[index];
            if ((filter == Filter::set && !row.set) || (filter == Filter::changed && !row.changed()) || !matches(row, query))
                continue;
            shownRows.push_back(index);
            // Changed values in the accent, values left to Hyprland faded.
            auto value = valueMarkup(row.value, row.description);
            if (row.changed())
                value = std::format("<span foreground=\"{}\" weight=\"600\">{}</span>", accent, value);
            else if (!row.set)
                value = std::format("<span alpha=\"60%\">{}</span>", value);
            made.push_back(RowList::Row{
                .cells  = {nameMarkup(row.description.name), value, escapeMarkup(row.description.description)},
                .marked = selected == row.description.name,
            });
        }
        list.setRows(std::move(made));
    }

    void OptionsPage::showDetails() {
        details->clearChildren();
        const auto found = selected ? std::ranges::find(rows, *selected, [](const auto& row) { return row.description.name; }) : rows.end();
        if (found == rows.end()) {
            details->addChild(markupLine("Values in the accent differ from Hyprland's default; faded ones are the default, left to Hyprland. Colors show as swatches.", palette,
                                         CFontSize{CFontSize::HT_FONT_SMALL}, 0.7F));
            return;
        }
        const auto& row  = *found;
        const auto& info = row.description;
        details->addChild(markupLine(std::format("<span weight=\"600\" font_family=\"{}\">{}</span>", escapeMarkup(palette->m_vars.fontFamilyMonospace), nameMarkup(info.name)),
                                     palette, CFontSize{CFontSize::HT_FONT_H3}, 1.0F));
        if (!info.description.empty())
            details->addChild(markupLine(escapeMarkup(info.description), palette));

        std::vector<std::string> facts;
        facts.push_back(std::format("Now {}", valueMarkup(row.value, info)));
        facts.push_back(std::format("default {}", valueMarkup(info.defaultValue, info)));
        if (info.minimum && info.maximum && info.choices.empty())
            facts.push_back(std::format("from {} to {}", ipc::numberText(*info.minimum), ipc::numberText(*info.maximum)));
        if (!info.choices.empty()) {
            std::string choices;
            for (const auto& [choice, number] : info.choices)
                choices += std::format("{}{} {}", choices.empty() ? "" : ", ", number, escapeMarkup(choice));
            facts.push_back(std::format("one of {}", choices));
        }
        if (row.where)
            facts.push_back(std::format("set at {}", colored(row.where->brief(), hex(palette->m_colors.linkText))));
        else if (row.set)
            facts.push_back("set by your config");
        else
            facts.push_back("left to Hyprland");
        std::string line;
        for (const auto& fact : facts)
            line += (line.empty() ? "" : "   ·   ") + fact;
        details->addChild(markupLine(line, palette, CFontSize{CFontSize::HT_FONT_TEXT}, 0.85F));
    }

}
