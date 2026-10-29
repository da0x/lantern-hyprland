// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/console-page.hpp"

#include "desktop/terminal-theme.hpp"
#include "foundation/environment.hpp"
#include "ipc/request.hpp"
#include "ui/bar-preview.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"
#include "ui/widgets.hpp"
#include "ui/typing.hpp"

#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>
#include <sstream>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr double rowHeight     = 28;
        constexpr double nameWidth     = 160;
        constexpr double requestsWidth = 480;
        constexpr double typedWidth    = 420;

        // Colors for when kitty's theme is not one Lantern knows: a plain dark terminal.
        ConsolePage::Terminal fallbackTerminal() {
            return {.background = "#1c1c1c",
                    .foreground = "#d0d0d0",
                    .colors    = {"#1c1c1c", "#e06c75", "#98c379", "#e5c07b", "#61afef", "#c678dd", "#56b6c2", "#d0d0d0", "#5c6370", "#e06c75", "#98c379",
                                   "#e5c07b", "#61afef", "#c678dd", "#56b6c2", "#ffffff"}};
        }

        // One line of indented JSON in a terminal's colors: keys in blue, strings in green,
        // numbers in yellow, and true, false and null in magenta.
        std::string jsonLine(std::string_view line, const ConsolePage::Terminal& terminal) {
            std::string made;
            std::size_t at = 0;
            while (at < line.size()) {
                const char c = line[at];
                if (c == '"') {
                    auto end = at + 1;
                    while (end < line.size() && line[end] != '"')
                        end += line[end] == '\\' ? 2 : 1;
                    end = std::min(end + 1, line.size());
                    auto after = end;
                    while (after < line.size() && line[after] == ' ')
                        ++after;
                    const bool key = after < line.size() && line[after] == ':';
                    made += colored(line.substr(at, end - at), terminal.colors[key ? 4 : 2]);
                    at = end;
                } else if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
                    auto end = at + 1;
                    while (end < line.size() && (std::isdigit(static_cast<unsigned char>(line[end])) || std::string_view{".eE+-"}.contains(line[end])))
                        ++end;
                    made += colored(line.substr(at, end - at), terminal.colors[3]);
                    at = end;
                } else if (std::isalpha(static_cast<unsigned char>(c))) {
                    auto end = at + 1;
                    while (end < line.size() && std::isalpha(static_cast<unsigned char>(line[end])))
                        ++end;
                    made += colored(line.substr(at, end - at), terminal.colors[5]);
                    at = end;
                } else {
                    made += colored(line.substr(at, 1), terminal.foreground);
                    ++at;
                }
            }
            return made;
        }

        std::string withoutTabs(std::string line) {
            for (auto at = line.find('\t'); at != std::string::npos; at = line.find('\t', at))
                line.replace(at, 1, "    ");
            return line;
        }

    }

    ConsolePage::ConsolePage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer) :
        palette{std::move(palette)},
        defer{defer},
        requests{this->palette, defer,
                 {RowList::Column{.width = nameWidth, .monospace = true, .secondary = false, .markup = false, .heading = "Request"},
                  RowList::Column{.width = 0, .monospace = false, .secondary = true, .markup = false, .heading = "What it tells"}},
                 rowHeight},
        reply{this->palette, defer, {RowList::Column{.width = 0, .monospace = true, .secondary = false, .markup = true, .heading = ""}}, 22,
              [this] { return paletteColor(terminal.foreground, 0.7F); }} {
        readTerminal();
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(8)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        title = pageTitle("Console", this->palette);
        root->addChild(title);
        root->addChild(paragraph("Ask Hyprland what it knows. Pick a request from the list or type it as you would for hyprctl. The console sends only requests "
                                 "that read, so nothing here changes your desktop.",
                                 this->palette));

        const auto toolbar = leftRow();
        typed              = Hyprtoolkit::CTextboxBuilder::begin()
                    ->placeholder("monitors, or getoption general:gaps_in")
                    ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {scaled(typedWidth), scaled(34)}})
                    ->commence();
        registerTextbox(typed);
        toolbar->addChild(typed);
        toolbar->addChild(space(1));
        toolbar->addChild(button("Send", 100, true, [this] { this->defer([this] { send(std::string{typed->currentText()}); }); }));
        root->addChild(toolbar);

        status = paragraph(" ", this->palette, true);
        root->addChild(status);

        const auto body = Hyprtoolkit::CRowLayoutBuilder::begin()->gap(16)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        const auto left = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_PERCENT, {scaled(requestsWidth), 1.0}})->commence();
        left->addChild(requests.element());
        // A terminal in kitty's colors. It takes what the list leaves, like Lantern's
        // panel beside its sidebar.
        screen = Hyprtoolkit::CRectangleBuilder::begin()
                     ->color([this] { return paletteColor(terminal.background); })
                     ->rounding(this->palette->m_vars.bigRounding)
                     ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})
                     ->commence();
        screen->setGrow(true, false);
        const auto inside = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        inside->setMargin(14);
        inside->addChild(reply.element());
        screen->addChild(inside);
        body->addChild(left);
        body->addChild(screen);
        root->addChild(body);

        requests.setClicked([this](std::size_t index) {
            const auto all = ipc::consoleRequests();
            if (index >= all.size())
                return;
            // getoption needs an option's name, so it is sent with one, ready to change.
            const std::string name = all[index].name == "getoption" ? "getoption general:gaps_in" : std::string{all[index].name};
            this->defer([this, name] {
                typed->setText(name);
                send(name);
            });
        });

        showRequests();
        showReply();
    }

    void ConsolePage::onAsk(Ask ask) {
        this->ask = std::move(ask);
    }

    void ConsolePage::restyle() {
        showRequests();
        showReply();
    }

    void ConsolePage::readTerminal() {
        const auto* theme = desktop::currentTerminal(foundation::currentEnvironment());
        terminal          = theme ? Terminal{.background = theme->background, .foreground = theme->foreground, .colors = theme->colors} : fallbackTerminal();
    }

    void ConsolePage::send(std::string text) {
        asked = text;
        if (const auto command = ipc::consoleCommand(text); !command || !ask) {
            answer = std::unexpected{command ? std::string{"Not connected to Hyprland yet."} : command.error()};
            showReply();
            return;
        }
        status->setText(escapeMarkup(std::format("Asking Hyprland: {}", text)));
        const auto number = ++sent;
        ask(std::move(text), [this, number](const std::expected<std::string, std::string>& came) {
            if (number != sent)
                return;
            answer = came ? std::expected<std::string, std::string>{*came} : std::unexpected{std::format("No reply: {}.", came.error())};
            defer([this] { showReply(); });
        });
    }

    void ConsolePage::showRequests() {
        std::vector<RowList::Row> rows;
        for (const auto& request : ipc::consoleRequests())
            rows.push_back(RowList::Row{.cells = {std::string{request.name}, std::string{request.tells}}, .marked = false});
        requests.setRows(std::move(rows));
    }

    void ConsolePage::showReply() {
        // kitty's theme may have changed on the Terminal page since.
        const auto before = terminal.background;
        readTerminal();
        if (terminal.background != before)
            screen->rebuild()->commence();

        const auto prompt = colored("$ ", terminal.colors[2]);
        const auto line   = [](std::string markup) { return RowList::Row{.cells = {std::move(markup)}, .marked = false}; };
        std::vector<RowList::Row> rows;
        std::size_t               replied = 0;
        if (asked.empty()) {
            status->setText(escapeMarkup("Nothing asked yet."));
        } else {
            rows.push_back(line(prompt + colored("hyprctl -j " + asked, terminal.foreground)));
            if (!answer) {
                status->setText(escapeMarkup(answer.error()));
                rows.push_back(line(colored(answer.error(), terminal.colors[1])));
            } else {
                const auto json = ipc::indentedJson(*answer);
                std::istringstream lines{json.value_or(*answer)};
                for (std::string text; std::getline(lines, text); ++replied)
                    rows.push_back(line(json ? jsonLine(text, terminal) : colored(withoutTabs(text), terminal.foreground)));
                status->setText(escapeMarkup(std::format("Hyprland's reply to {}: {} {}.", asked, replied, replied == 1 ? "line" : "lines")));
            }
        }
        rows.push_back(line(prompt + colored("\u2588", terminal.foreground)));
        reply.setRows(std::move(rows));
    }

    void ConsolePage::embed() {
        root->removeChild(title);
        root->setMargin(0);
    }

}
