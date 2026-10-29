// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/night-light-page.hpp"

#include "application/command.hpp"
#include "application/config-check.hpp"
#include "application/lantern-folder.hpp"
#include "configuration/autostart.hpp"
#include "configuration/main-config.hpp"
#include "desktop/process.hpp"
#include "desktop/system.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr auto changeName = "Night light";

        std::optional<std::filesystem::path> program(std::string_view name) {
            return desktop::findProgram(name, foundation::variable(foundation::currentEnvironment(), "PATH").value_or(""));
        }

        std::string fileText(const std::filesystem::path& file) {
            std::ifstream stream{file, std::ios::binary};
            std::stringstream text;
            text << stream.rdbuf();
            return text.str();
        }

        // The choices, with the one the config has when it is none of them.
        template <typename Value>
        std::vector<Value> withCurrent(std::vector<Value> choices, const Value& now) {
            if (std::ranges::find(choices, now) == choices.end()) {
                choices.push_back(now);
                std::ranges::sort(choices);
            }
            return choices;
        }

        std::string warmthText(int kelvin) {
            switch (kelvin) {
                case 5500: return "A little warmer, 5500 K";
                case 4500: return "Warm, 4500 K";
                case 3500: return "Warmer, 3500 K";
                case 2500: return "Warmest, 2500 K";
                default: return std::format("{} K", kelvin);
            }
        }

    }

    NightLightPage::NightLightPage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop) :
        palette{std::move(palette)},
        defer{std::move(defer)},
        loop{std::move(loop)},
        diff{this->palette, this->defer,
             {RowList::Column{.width = 24, .monospace = true, .secondary = false, .markup = true, .heading = ""},
              RowList::Column{.width = 0, .monospace = true, .secondary = false, .markup = true, .heading = ""}},
             20} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        title = pageTitle("Night light", this->palette);
        root->addChild(title);
        root->addChild(paragraph("Turns the screens warmer in the evening, with less blue light, and back in the morning. hyprsunset does it; Lantern writes its "
                                 "settings after you review them.",
                                 this->palette));
        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        root->addChild(body);
        footer = leftRow();
        root->addChild(footer);
        refresh();
    }

    void NightLightPage::refresh() {
        if (reviewing)
            return;
        const auto light  = desktop::nightLightIn(fileText(desktop::hyprsunsetConfigFile(foundation::currentEnvironment())));
        const auto config = configuration::mainConfig(configuration::lanternLaunch());
        onNow             = light && config && configuration::startsAtLogin(fileText(config->file), config->lua(), "hyprsunset");
        now               = light.value_or(desktop::NightLight{});
        onWanted          = onNow;
        wanted            = now;
        showOverview();
    }

    void NightLightPage::restyle() {
        if (!reviewing)
            showOverview();
    }

    void NightLightPage::leave() {
        if (trying)
            tryWarmth(std::nullopt);
    }

    void NightLightPage::tryWarmth(std::optional<int> temperature) {
        const auto hyprctl = program("hyprctl");
        if (!hyprctl)
            return;
        trying = temperature.has_value();
        const std::vector<std::string> arguments = temperature ? std::vector<std::string>{"hyprsunset", "temperature", std::to_string(*temperature)}
                                                               : std::vector<std::string>{"hyprsunset", "reset"};
        (void)application::runCommand(loop, *hyprctl, arguments, [](bool, std::string) {});
    }

    std::expected<std::vector<configuration::FileChange>, std::string> NightLightPage::plan() const {
        std::vector<configuration::FileChange> planned;
        if (onWanted && (wanted != now || !onNow)) {
            auto sunset = configuration::planEdit(desktop::hyprsunsetConfigFile(foundation::currentEnvironment()),
                                                  [this](std::string_view text) { return desktop::withNightLight(text, wanted); });
            if (!sunset)
                return std::unexpected{sunset.error()};
            if (*sunset)
                planned.push_back(std::move(**sunset));
        }
        if (onWanted != onNow) {
            const auto config = configuration::mainConfig(configuration::lanternLaunch());
            if (!config)
                return std::unexpected{config.error()};
            auto start = configuration::planEdit(config->file, [lua = config->lua(), on = onWanted](std::string_view text) {
                return on ? configuration::withAutostart(text, lua, "hyprsunset", {}) : configuration::withoutAutostart(text, lua, "hyprsunset");
            });
            if (!start)
                return std::unexpected{start.error()};
            if (*start)
                planned.push_back(std::move(**start));
        }
        return planned;
    }

    void NightLightPage::showOverview() {
        reviewing = false;
        body->clearChildren();
        body->addChild(space(4));
        const bool installed = program("hyprsunset").has_value();
        if (!installed)
            body->addChild(paragraph("hyprsunset is not installed, so the night light cannot turn on yet. Install it with sudo pacman -S hyprsunset; Lantern starts "
                                     "it once this is applied.",
                                     palette, true));
        body->addChild(settingRow("Night light", "whether the screens turn warmer at night", {"Off", "On"}, onWanted ? 1 : 0, palette,
                                  [this](std::size_t chosen) {
                                      onWanted = chosen == 1;
                                      showOverview();
                                  },
                                  defer));
        if (onWanted) {
            const auto warmths = withCurrent<int>({5500, 4500, 3500, 2500}, wanted.temperature);
            std::vector<std::string> warmthItems;
            for (const auto kelvin : warmths)
                warmthItems.push_back(warmthText(kelvin));
            body->addChild(settingRow("Warmth", "how much blue light is taken out", std::move(warmthItems),
                                      static_cast<std::size_t>(std::ranges::find(warmths, wanted.temperature) - warmths.begin()), palette,
                                      [this, warmths](std::size_t chosen) {
                                          if (chosen < warmths.size())
                                              wanted.temperature = warmths[chosen];
                                          if (trying)
                                              tryWarmth(wanted.temperature);
                                          showOverview();
                                      },
                                      defer));
            const auto timeRow = [this](const std::string& name, const std::string& about, std::vector<std::string> choices, std::string desktop::NightLight::*setting) {
                const auto shown = withCurrent(std::move(choices), wanted.*setting);
                body->addChild(settingRow(name, about, shown, static_cast<std::size_t>(std::ranges::find(shown, wanted.*setting) - shown.begin()), palette,
                                          [this, shown, setting](std::size_t chosen) {
                                              if (chosen < shown.size())
                                                  wanted.*setting = shown[chosen];
                                              showOverview();
                                          },
                                          defer));
            };
            timeRow("Turns on at", "in the evening", {"18:00", "19:00", "20:00", "21:00", "22:00", "23:00"}, &desktop::NightLight::evening);
            timeRow("Turns off at", "in the morning", {"05:00", "06:00", "07:00", "08:00", "09:00"}, &desktop::NightLight::morning);
            if (installed && !desktop::findProcesses("hyprsunset").empty()) {
                const auto row = leftRow(scaled(40.0));
                row->addChild(button(trying ? "Back to normal" : "Try it now", 160, false, [this] {
                    defer([this] {
                        tryWarmth(trying ? std::nullopt : std::optional<int>{wanted.temperature});
                        showOverview();
                    });
                }));
                row->addChild(markupLine(trying ? "The screens show the warmth chosen." : "Shows the warmth on the screens, until you leave this page.", palette));
                body->addChild(space(6));
                body->addChild(row);
            }
        }
        fillStatus();
    }

    void NightLightPage::fillStatus() {
        const auto                                               planned = plan();
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> buttons;
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> away;
        if (configuration::latestChange(application::backupsFolder(foundation::currentEnvironment()), changeName))
            away.push_back(button("Undo last change", 160, false, [this] { defer([this] { undo(); }); }));
        std::string note;
        if (!planned)
            note = escapeMarkup(std::format("Lantern cannot apply it: {}.", planned.error()));
        else if (planned->empty())
            note = std::format("{}  {}", colored("✓", hex(palette->m_colors.accent)),
                               escapeMarkup(!outcome.empty() ? outcome : onNow ? std::format("On from {} to {}.", now.evening, now.morning) : "The night light is off."));
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

    void NightLightPage::showReview() {
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
        body->addChild(heading("Review: night light", palette));
        body->addChild(paragraph("Applying writes the changes below, after keeping a copy of each file so they can be undone, and starts hyprsunset again so that it "
                                 "reads them.",
                                 palette, true));
        setFooter(footer, {button("Cancel", 110, false, [this] { defer([this] { showOverview(); }); }), button("Apply", 110, true, [this] { defer([this] { apply(); }); })});
        diff.setRows(diffRows(changes, *palette));
        body->addChild(diff.element());
    }

    void NightLightPage::apply() {
        if (!reviewing)
            return;
        const auto environment = foundation::currentEnvironment();
        const auto record      = configuration::applyChanges(changes, application::backupsFolder(environment), changeName);
        reviewing              = false;
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
        trying = false;
        refresh();
        restart();
    }

    void NightLightPage::restart() {
        for (const auto process : desktop::findProcesses("hyprsunset"))
            (void)desktop::stopProcess(process);
        const auto hyprsunset = program("hyprsunset");
        if (!hyprsunset || !onNow)
            return;
        // A moment for the old one to exit first.
        loop.after(std::chrono::milliseconds{300}, [hyprsunset = *hyprsunset] { (void)desktop::startDetached(hyprsunset); });
    }

    void NightLightPage::undo() {
        const auto latest = configuration::latestChange(application::backupsFolder(foundation::currentEnvironment()), changeName);
        if (!latest)
            return showOverview();
        if (const auto undone = configuration::undoChange(*latest); !undone)
            outcome = std::format("Nothing was undone: {}.", undone.error());
        else
            outcome = "Undone.";
        refresh();
        restart();
    }

    void NightLightPage::embed() {
        root->removeChild(title);
        root->setMargin(0);
    }

}
