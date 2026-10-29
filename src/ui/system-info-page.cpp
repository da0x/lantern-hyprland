// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/system-info-page.hpp"

#include "application/command.hpp"
#include "application/lantern-folder.hpp"
#include "desktop/clipboard.hpp"
#include "desktop/fastfetch.hpp"
#include "desktop/system.hpp"
#include "desktop/terminal-theme.hpp"
#include "ui/bar-preview.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"
#include "ui/widgets.hpp"

#include <hyprtoolkit/element/Rectangle.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprtoolkit::CFontSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr auto changeName = "System info";

        std::optional<std::filesystem::path> program(std::string_view name) {
            return desktop::findProgram(name, foundation::variable(foundation::currentEnvironment(), "PATH").value_or(""));
        }

        std::optional<std::string> fileText(const std::filesystem::path& file) {
            std::ifstream stream{file, std::ios::binary};
            if (!stream)
                return std::nullopt;
            std::stringstream text;
            text << stream.rdbuf();
            return text.str();
        }

        // What fastfetch says when nothing has run it yet, to show the colors on.
        const std::vector<std::string> sample{"you@archlinux", "-------------", "OS  Arch Linux x86_64", "Kernel  Linux", "WM  Hyprland (Wayland)", "Uptime  1 hour",
                                              "Packages  1000 (pacman)", "Memory  4 GiB / 16 GiB"};

    }

    SystemInfoPage::SystemInfoPage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop) :
        palette{std::move(palette)},
        defer{std::move(defer)},
        loop{std::move(loop)},
        diff{this->palette, this->defer,
             {RowList::Column{.width = 24, .monospace = true, .secondary = false, .markup = true, .heading = ""},
              RowList::Column{.width = 0, .monospace = true, .secondary = false, .markup = true, .heading = ""}},
             20} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->addChild(paragraph("fastfetch shows a summary of your system in the terminal: the distribution's logo beside what it is and runs, in your "
                                 "terminal scheme's own colors. Lantern writes its settings after you review them.",
                                 this->palette));
        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        root->addChild(body);
        footer = leftRow();
        root->addChild(footer);
        refresh();
    }

    void SystemInfoPage::refresh() {
        if (reviewing)
            return;
        const auto environment = foundation::currentEnvironment();
        now                    = desktop::fastfetchIn(fileText(desktop::fastfetchConfigFile(environment)).value_or(""));
        wanted                 = now.value_or(desktop::Fastfetch{});
        greetsNow              = desktop::greetsWithFastfetch(fileText(desktop::bashrcFile(environment)).value_or(""));
        greetsWanted           = greetsNow;
        showOverview();
    }

    void SystemInfoPage::restyle() {
        if (!reviewing)
            showOverview();
    }

    std::expected<std::vector<configuration::FileChange>, std::string> SystemInfoPage::plan() const {
        const auto                             environment = foundation::currentEnvironment();
        std::vector<configuration::FileChange> planned;
        if (now != wanted) {
            auto config = configuration::planFile(desktop::fastfetchConfigFile(environment), desktop::fastfetchConfig(wanted));
            if (!config)
                return std::unexpected{config.error()};
            if (config->changes())
                planned.push_back(std::move(*config));
        }
        if (greetsNow != greetsWanted) {
            auto bashrc = configuration::planEdit(desktop::bashrcFile(environment), [on = greetsWanted](std::string_view text) { return desktop::withFastfetchGreeting(text, on); });
            if (!bashrc)
                return std::unexpected{bashrc.error()};
            if (*bashrc)
                planned.push_back(std::move(**bashrc));
        }
        return planned;
    }

    void SystemInfoPage::showOverview() {
        reviewing = false;
        body->clearChildren();
        body->addChild(space(4));
        if (!program("fastfetch"))
            body->addChild(requirementBlock(desktop::Requirement{.name     = "fastfetch",
                                                                 .purpose  = "Prints the summary of the system.",
                                                                 .upstream = "https://github.com/fastfetch-cli/fastfetch",
                                                                 .package  = "fastfetch",
                                                                 .found    = std::nullopt},
                                            desktop::currentDistribution(),
                                            desktop::clipboardProgram(foundation::variable(foundation::currentEnvironment(), "PATH").value_or("")), palette, defer, [this] {
                                                linesFor.reset();
                                                refresh();
                                            }));
        if (!now && fileText(desktop::fastfetchConfigFile(foundation::currentEnvironment())))
            body->addChild(paragraph("You have a fastfetch config of your own. Applying replaces it; Undo puts it back.", palette, true));

        // Two columns: the settings, and what the terminal would show.
        const auto columns = leftRow(0);
        columns->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->gap(32)->commence();
        const auto left = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {0.48, 1.0}})->commence();
        preview         = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {0.48, 1.0}})->commence();
        columns->addChild(left);
        columns->addChild(preview);
        body->addChild(columns);

        const auto changed = [this] {
            showOverview();
        };
        left->addChild(settingRow("Logo", "the distribution's, beside it", {"Large", "Small", "None"}, static_cast<std::size_t>(wanted.logo), palette,
                                  [this, changed](std::size_t chosen) {
                                      wanted.logo = static_cast<desktop::FetchLogo>(std::min<std::size_t>(chosen, 2));
                                      changed();
                                  },
                                  defer));
        std::vector<std::string> accents;
        std::size_t              accent = 0;
        for (const auto& choice : desktop::fetchAccents) {
            if (choice.name == wanted.accent)
                accent = accents.size();
            accents.emplace_back(choice.label);
        }
        left->addChild(settingRow("Color", "from your terminal's scheme", std::move(accents), accent, palette,
                                  [this, changed](std::size_t chosen) {
                                      if (chosen < desktop::fetchAccents.size())
                                          wanted.accent = std::string{desktop::fetchAccents[chosen].name};
                                      changed();
                                  },
                                  defer));
        left->addChild(settingRow("Shows", "how much it tells", {"The main things", "Everything"}, wanted.full ? 1 : 0, palette,
                                  [this, changed](std::size_t chosen) {
                                      wanted.full = chosen == 1;
                                      changed();
                                  },
                                  defer));
        left->addChild(settingRow("New terminals", "open with it, through ~/.bashrc", {"Off", "On"}, greetsWanted ? 1 : 0, palette,
                                  [this, changed](std::size_t chosen) {
                                      greetsWanted = chosen == 1;
                                      changed();
                                  },
                                  defer));
        fillPreview();
        fillStatus();
    }

    void SystemInfoPage::fillPreview() {
        preview->clearChildren();
        const auto theme      = desktop::currentTerminal(foundation::currentEnvironment());
        const auto background = theme ? theme->background : hex(palette->m_colors.base);
        const auto foreground = theme ? theme->foreground : hex(palette->m_colors.text);
        const auto accent     = std::ranges::find(desktop::fetchAccents, wanted.accent, &desktop::FetchAccent::name);
        const auto color     = theme && accent != desktop::fetchAccents.end() ? theme->colors[accent->color] : hex(palette->m_colors.accent);

        // fastfetch's own words, once it has run with these settings; until then, a sample.
        const auto fastfetch = program("fastfetch");
        if (fastfetch && linesFor != wanted) {
            linesFor        = wanted;
            const auto file = std::filesystem::temp_directory_path() / std::format("lantern-hyprland-fastfetch-{}.jsonc", ::getpid());
            if (std::ofstream{file} << desktop::fastfetchConfig(wanted))
                (void)application::runCommand(loop, *fastfetch, {"--config", file.string(), "--logo", "none", "--pipe", "true"},
                                              [this, file, shown = wanted](bool ok, std::string output) {
                                                  std::error_code error;
                                                  std::filesystem::remove(file, error);
                                                  if (!ok || shown != wanted)
                                                      return;
                                                  lines.clear();
                                                  std::istringstream stream{output};
                                                  for (std::string line; std::getline(stream, line);)
                                                      if (!line.empty())
                                                          lines.push_back(line);
                                                  if (!reviewing)
                                                      fillPreview();
                                              });
        }
        const auto& shown = fastfetch && !lines.empty() ? lines : sample;

        const auto frame = Hyprtoolkit::CRectangleBuilder::begin()
                               ->color([value = paletteColor(background)] { return value; })
                               ->rounding(palette->m_vars.smallRounding)
                               ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})
                               ->commence();
        const auto text = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(2)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        text->setMargin(14);
        const auto mono = escapeMarkup(palette->m_vars.fontFamilyMonospace);
        for (std::size_t index = 0; index < shown.size(); ++index) {
            const auto& line = shown[index];
            std::string markup;
            // The title, user@host, is in the color; the keys before the separator are too. The
            // rows of color blocks are in the terminal's own colors.
            if (line.contains('\x1b')) {
                for (const auto& run : desktop::fetchRuns(line)) {
                    const auto in = [&](const std::optional<std::size_t>& color, const std::string& otherwise) {
                        return theme && color && *color < theme->colors.size() ? theme->colors[*color] : otherwise;
                    };
                    const auto back = in(run.background, "");
                    markup += std::format("<span foreground=\"{}\"{}>{}</span>", in(run.foreground, foreground),
                                          back.empty() ? "" : std::format(" background=\"{}\"", back), escapeMarkup(run.text));
                }
            } else if (index == 0 || line.starts_with("---"))
                markup = colored(line, index == 0 ? color : foreground);
            else if (const auto gap = line.find("  "); gap != std::string::npos)
                markup = colored(line.substr(0, gap), color) + colored(line.substr(gap), foreground);
            else
                markup = colored(line, foreground);
            text->addChild(markupLine(std::format("<span font_family=\"{}\">{}</span>", mono, markup), palette, CFontSize{CFontSize::HT_FONT_SMALL}, 1.0F));
        }
        frame->addChild(text);
        preview->addChild(frame);
        if (wanted.logo != desktop::FetchLogo::none)
            preview->addChild(markupLine("The logo is drawn beside it, in the same color.", palette, CFontSize{CFontSize::HT_FONT_SMALL}, 0.6F));
    }

    void SystemInfoPage::fillStatus() {
        const auto                                               planned = plan();
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> buttons;
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> away;
        if (configuration::latestChange(application::backupsFolder(foundation::currentEnvironment()), changeName))
            away.push_back(button("Undo last change", 160, false, [this] { defer([this] { undo(); }); }));
        std::string note;
        if (!planned)
            note = escapeMarkup(std::format("Lantern cannot apply it: {}.", planned.error()));
        else if (planned->empty())
            note = now ? std::format("{}  {}", colored("✓", hex(palette->m_colors.accent)), escapeMarkup(outcome.empty() ? "fastfetch is set like this." : outcome))
                       : escapeMarkup(outcome);
        if (planned && (!planned->empty() || !now))
            buttons.push_back(button("Review changes", 160, true, [this] { defer([this] { showReview(); }); }));
        CSharedPointer<Hyprtoolkit::IElement> noteText;
        if (!note.empty()) {
            const auto made = markupLine(note, palette);
            made->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
            noteText = made;
        }
        setFooter(footer, buttons, noteText, away);
    }

    void SystemInfoPage::showReview() {
        auto planned = plan();
        if (planned && planned->empty() && !now) {
            // Nothing chosen yet differs from Lantern's defaults, but there is no config of
            // Lantern's: write it as shown.
            auto config = configuration::planFile(desktop::fastfetchConfigFile(foundation::currentEnvironment()), desktop::fastfetchConfig(wanted));
            if (config && config->changes())
                planned->push_back(std::move(*config));
        }
        if (!planned || planned->empty()) {
            outcome = planned ? "" : planned.error();
            return showOverview();
        }
        reviewing = true;
        changes   = std::move(*planned);
        outcome.clear();
        body->clearChildren();
        body->addChild(space(4));
        body->addChild(heading("Review: system info", palette));
        body->addChild(paragraph("Applying writes the changes below, after keeping a copy of each file so they can be undone. A terminal opened after "
                                 "that shows them.",
                                 palette, true));
        setFooter(footer, {button("Cancel", 110, false, [this] { defer([this] { showOverview(); }); }), button("Apply", 110, true, [this] { defer([this] { apply(); }); })});
        diff.setRows(diffRows(changes, *palette));
        body->addChild(diff.element());
    }

    void SystemInfoPage::apply() {
        if (!reviewing)
            return;
        const auto record = configuration::applyChanges(changes, application::backupsFolder(foundation::currentEnvironment()), changeName);
        reviewing         = false;
        outcome           = record ? "Applied. A new terminal shows it." : std::format("Nothing was written: {}.", record.error());
        refresh();
    }

    void SystemInfoPage::undo() {
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
