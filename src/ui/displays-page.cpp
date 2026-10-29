// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/displays-page.hpp"

#include "application/command.hpp"
#include "application/config-check.hpp"
#include "application/lantern-folder.hpp"
#include "configuration/main-config.hpp"
#include "desktop/system.hpp"
#include "ui/bar-preview.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"
#include "ui/widgets.hpp"

#include <hyprtoolkit/element/Combobox.hpp>
#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/Text.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <ranges>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprtoolkit::CFontSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr auto   changeName = "Displays";
        // How long a change is tried before it is put back, unless it is kept.
        constexpr int    trialSeconds = 15;
        constexpr double mapHeight    = 170;
        constexpr double mapWidth     = 560;

        CDynamicSize fixed(double width, double height) {
            return CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {width, height}};
        }

        // Placed `at` from its parent's top left corner, rather than laid out.
        void place(const CSharedPointer<Hyprtoolkit::IElement>& element, double x, double y) {
            element->setPositionMode(Hyprtoolkit::IElement::HT_POSITION_ABSOLUTE);
            element->setAbsolutePosition({x, y});
        }

        // 60, or 59.95: how a refresh rate is written.
        std::string rateText(double rate) {
            auto text = std::format("{:.2f}", rate);
            while (text.ends_with('0'))
                text.pop_back();
            if (text.ends_with('.'))
                text.pop_back();
            return text;
        }

        std::optional<std::filesystem::path> hyprctl() {
            return desktop::findProgram("hyprctl", foundation::variable(foundation::currentEnvironment(), "PATH").value_or(""));
        }

        // The maker and model without the serial number Hyprland adds at the end.
        std::string shortName(const desktop::Display& display) {
            auto words = display.description;
            if (const auto space = words.rfind(' '); space != std::string::npos && words.size() - space > 6)
                words.resize(space);
            return words.empty() ? display.name : words;
        }

        std::string rotationName(int transform) {
            switch (transform) {
                case 0: return "Upright";
                case 1: return "Turned 90°";
                case 2: return "Upside down";
                case 3: return "Turned 270°";
                default: return std::format("Flipped ({})", transform);
            }
        }

    }

    DisplaysPage::DisplaysPage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop) :
        palette{std::move(palette)},
        defer{std::move(defer)},
        loop{std::move(loop)},
        diff{this->palette, this->defer,
             {RowList::Column{.width = 24, .monospace = true, .secondary = false, .markup = true, .heading = ""},
              RowList::Column{.width = 0, .monospace = true, .secondary = false, .markup = true, .heading = ""}},
             20} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        title = pageTitle("Displays", this->palette);
        root->addChild(title);
        root->addChild(paragraph("Each display's resolution, refresh rate, scale, rotation and place beside the others. Nothing is written until you review the change, "
                                 "and a change is put back after 15 seconds unless you keep it, in case a display goes dark.",
                                 this->palette));
        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        root->addChild(body);
        footer = leftRow();
        root->addChild(footer);
        refresh();
    }

    void DisplaysPage::refresh() {
        if (reviewing || trying)
            return;
        const auto program = hyprctl();
        if (!program) {
            problem = "hyprctl is not installed, so Lantern cannot ask Hyprland for its displays";
            return showOverview();
        }
        const auto started = application::runCommand(loop, *program, {"-j", "monitors", "all"}, [this](bool succeeded, std::string output) {
            auto read = succeeded ? desktop::parseDisplays(output) : std::unexpected{std::string{"Hyprland did not answer"}};
            if (!read) {
                problem = read.error();
            } else {
                problem.clear();
                // A choice not yet reviewed stays, while the same displays are there.
                const bool same = read->size() == displays.size() && std::ranges::equal(*read, displays, {}, &desktop::Display::name, &desktop::Display::name);
                displays        = std::move(*read);
                if (!same) {
                    const auto config = configuration::mainConfig(configuration::lanternLaunch());
                    sharpNow          = config ? desktop::currentSharpApps(*config) : std::nullopt;
                    sharpWanted       = sharpNow && sharpNow->on;
                    wanted.clear();
                    for (const auto& display : displays)
                        wanted.push_back(display.settings);
                    selected = 0;
                }
            }
            if (!reviewing)
                showOverview();
        });
        if (!started) {
            problem = started.error();
            showOverview();
        }
    }

    void DisplaysPage::restyle() {
        if (!reviewing)
            showOverview();
    }

    std::expected<std::optional<configuration::FileChange>, std::string> DisplaysPage::plan() const {
        const auto config = configuration::mainConfig(configuration::lanternLaunch());
        if (!config)
            return std::unexpected{config.error()};
        // Hyprland's setting when it is switched, and GTK's scale again when it is on and a
        // display's scale changes, so a GDK_SCALE of the user's own is otherwise left alone.
        std::optional<desktop::SharpApps> sharp;
        const bool rescaled = std::ranges::any_of(std::views::iota(std::size_t{0}, displays.size()), [this](std::size_t index) { return wanted[index].scale != displays[index].settings.scale; });
        if (sharpNow && (sharpWanted != sharpNow->on || (sharpWanted && rescaled)))
            sharp = desktop::sharpAppsFor(sharpWanted, wanted);
        return desktop::planDisplays(displays, wanted, sharp, *config);
    }

    void DisplaysPage::showOverview() {
        reviewing = false;
        body->clearChildren();
        if (!problem.empty()) {
            body->addChild(paragraph(std::format("Lantern cannot read your displays: {}.", problem), palette));
            fillStatus();
            return;
        }
        if (displays.empty()) {
            body->addChild(paragraph("Asking Hyprland for its displays…", palette, true));
            return;
        }
        selected = std::min(selected, displays.size() - 1);
        fillMap();
        fillSettings();
        fillSharpApps();
        fillStatus();
    }

    void DisplaysPage::fillMap() {
        // The displays as they will be laid out, as large as fits, the chosen one outlined.
        int left = 0, top = 0, right = 0, bottom = 0;
        bool first = true;
        for (const auto& settings : wanted) {
            if (!settings.enabled)
                continue;
            const auto [width, height] = desktop::layoutSize(settings);
            left   = first ? settings.x : std::min(left, settings.x);
            top    = first ? settings.y : std::min(top, settings.y);
            right  = first ? settings.x + width : std::max(right, settings.x + width);
            bottom = first ? settings.y + height : std::max(bottom, settings.y + height);
            first  = false;
        }
        const double spanX = std::max(1, right - left);
        const double spanY = std::max(1, bottom - top);
        const double fit   = std::min(scaled(mapWidth) / spanX, mapHeight / spanY);

        const auto& colors  = palette->m_colors;
        const auto  backdrop = darkTheme(*palette) ? paletteColor("#1d2021") : paletteColor("#c9cdd6");
        const auto  canvas   = Hyprtoolkit::CNullBuilder::begin()->size(fixed(spanX * fit, spanY * fit))->commence();
        for (std::size_t index = 0; index < displays.size(); ++index) {
            const auto& settings = wanted[index];
            if (!settings.enabled)
                continue;
            const auto [width, height] = desktop::layoutSize(settings);
            const bool chosen          = index == selected && displays.size() > 1;
            const auto border          = chosen ? colors.accent : colors.alternateBase;
            const auto screen          = Hyprtoolkit::CRectangleBuilder::begin()
                                    ->color([backdrop] { return backdrop; })
                                    ->borderColor([border] { return border; })
                                    ->borderThickness(chosen ? 3 : 2)
                                    ->rounding(6)
                                    ->size(fixed(std::max(8.0, width * fit - 4), std::max(8.0, height * fit - 4)))
                                    ->commence();
            place(screen, (settings.x - left) * fit + 2, (settings.y - top) * fit + 2);
            const auto name = Hyprtoolkit::CTextBuilder::begin()
                                  ->text(std::format("<span weight=\"600\">{}</span>  {} × {}", escapeMarkup(displays[index].name), settings.mode.width, settings.mode.height))
                                  ->fontSize(CFontSize{CFontSize::HT_FONT_SMALL})
                                  ->color([text = colors.text] { return text; })
                                  ->align(Hyprtoolkit::HT_FONT_ALIGN_LEFT)
                                  ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {std::max(8.0, width * fit - 16), 1.0}})
                                  ->commence();
            place(name, 6, std::max(4.0, height * fit - 26));
            screen->addChild(name);
            canvas->addChild(screen);
        }
        const auto map = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, mapHeight}})->commence();
        map->addChild(canvas);
        body->addChild(map);
        body->addChild(space(4));

        // Which display the settings below are for, when there is more than one.
        if (displays.size() > 1) {
            const auto pick = leftRow(scaled(36.0));
            pick->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, scaled(36.0)}})->gap(8)->commence();
            for (std::size_t index = 0; index < displays.size(); ++index)
                pick->addChild(button(std::format("{} · {}", displays[index].name, shortName(displays[index])), scaled(260.0), index == selected, [this, index] {
                    defer([this, index] {
                        selected = index;
                        showOverview();
                    });
                }));
            body->addChild(pick);
        }
    }

    void DisplaysPage::fillSettings() {
        const auto& display  = displays[selected];
        auto&       settings = wanted[selected];
        const auto  changed  = [this] { showOverview(); };

        body->addChild(heading(std::format("{} · {}", display.name, shortName(display)), palette));

        const auto enabledCount = std::ranges::count_if(wanted, &desktop::DisplaySettings::enabled);
        if (displays.size() > 1 && (!settings.enabled || enabledCount > 1))
            body->addChild(settingRow("Turned on", "whether Hyprland uses this display", {"On", "Off"}, settings.enabled ? 0 : 1, palette,
                                      [this, changed](std::size_t index) {
                                          wanted[selected].enabled = index == 0;
                                          if (wanted[selected].enabled && wanted[selected].mode.width == 0 && !displays[selected].modes.empty())
                                              wanted[selected].mode = displays[selected].modes.front();
                                          changed();
                                      },
                                      defer));
        if (!settings.enabled)
            return;

        // Resolutions, each once, then the refresh rates the chosen one has.
        std::vector<std::pair<int, int>> sizes;
        for (const auto& mode : display.modes)
            if (std::ranges::find(sizes, std::pair{mode.width, mode.height}) == sizes.end())
                sizes.emplace_back(mode.width, mode.height);
        if (std::ranges::find(sizes, std::pair{settings.mode.width, settings.mode.height}) == sizes.end())
            sizes.insert(sizes.begin(), {settings.mode.width, settings.mode.height});
        std::vector<std::string> sizeNames;
        for (std::size_t index = 0; index < sizes.size(); ++index)
            sizeNames.push_back(std::format("{} × {}{}", sizes[index].first, sizes[index].second, index == 0 ? "  (largest)" : ""));
        const auto sizeAt = static_cast<std::size_t>(std::ranges::find(sizes, std::pair{settings.mode.width, settings.mode.height}) - sizes.begin());
        body->addChild(settingRow("Resolution", "how many pixels it shows", sizeNames, sizeAt, palette,
                                  [this, sizes, changed](std::size_t index) {
                                      auto& now = wanted[selected];
                                      // The fastest rate the new resolution has.
                                      const auto& modes = displays[selected].modes;
                                      const auto  found = std::ranges::find_if(modes, [&](const auto& mode) { return std::pair{mode.width, mode.height} == sizes[index]; });
                                      now.mode          = found != modes.end() ? *found : desktop::Mode{sizes[index].first, sizes[index].second, now.mode.refresh};
                                      const auto scales = desktop::scalesFor(now.mode, 0);
                                      if (std::ranges::find(scales, now.scale) == scales.end())
                                          now.scale = 1;
                                      changed();
                                  },
                                  defer));

        std::vector<double> rates;
        for (const auto& mode : display.modes)
            if (mode.width == settings.mode.width && mode.height == settings.mode.height && std::ranges::find(rates, mode.refresh) == rates.end())
                rates.push_back(mode.refresh);
        if (std::ranges::find(rates, settings.mode.refresh) == rates.end())
            rates.push_back(settings.mode.refresh);
        std::ranges::sort(rates, std::greater{});
        std::vector<std::string> rateNames;
        for (const auto rate : rates)
            rateNames.push_back(std::format("{} Hz", rateText(rate)));
        const auto rateAt = static_cast<std::size_t>(std::ranges::find(rates, settings.mode.refresh) - rates.begin());
        body->addChild(settingRow("Refresh rate", "how many times a second it redraws", rateNames, rateAt, palette,
                                  [this, rates, changed](std::size_t index) {
                                      wanted[selected].mode.refresh = rates[index];
                                      changed();
                                  },
                                  defer));

        // A large display that offers no more than 30 Hz is usually held back by its cable,
        // dock or port, which no setting changes.
        if (rates.front() < 50 && settings.mode.width >= 3840)
            body->addChild(markupLine(escapeMarkup(std::format("{} offers no more than {} Hz at this resolution through its connection. A cable, port or dock that "
                                                               "carries more, such as DisplayPort 1.4 or HDMI 2.1, or a lower resolution, allows a faster rate.",
                                                               display.name, rateText(rates.front()))),
                                      palette, CFontSize{CFontSize::HT_FONT_SMALL}, 0.7F));

        const auto               scales = desktop::scalesFor(settings.mode, settings.scale);
        std::vector<std::string> scaleNames;
        for (const auto scale : scales) {
            const auto [width, height] = desktop::layoutSize(desktop::DisplaySettings{.mode = settings.mode, .scale = scale});
            scaleNames.push_back(std::format("{:g}%  · like {} × {}", std::round(scale * 1000) / 10, width, height));
        }
        const auto scaleAt = static_cast<std::size_t>(std::ranges::find(scales, settings.scale) - scales.begin());
        body->addChild(settingRow("Scale", "how large everything is drawn", scaleNames, scaleAt, palette,
                                  [this, scales, changed](std::size_t index) {
                                      wanted[selected].scale = scales[index];
                                      changed();
                                  },
                                  defer));

        std::vector<int> turns{0, 1, 2, 3};
        if (settings.transform > 3)
            turns.push_back(settings.transform);
        std::vector<std::string> turnNames;
        for (const auto turn : turns)
            turnNames.push_back(rotationName(turn));
        body->addChild(settingRow("Rotation", "for a display turned on its stand", turnNames,
                                  static_cast<std::size_t>(std::ranges::find(turns, settings.transform) - turns.begin()), palette,
                                  [this, turns, changed](std::size_t index) {
                                      wanted[selected].transform = turns[index];
                                      changed();
                                  },
                                  defer));

        body->addChild(settingRow("Variable refresh rate", "smoother games and video on displays that support it", {"Off", "On", "Full-screen windows only"},
                                  static_cast<std::size_t>(std::clamp(settings.vrr, 0, 2)), palette,
                                  [this, changed](std::size_t index) {
                                      wanted[selected].vrr = static_cast<int>(index);
                                      changed();
                                  },
                                  defer));

        // Beside each other display that is on.
        if (enabledCount > 1) {
            struct Place {
                std::string name;
                int         x, y;
            };
            std::vector<Place> places{{std::format("Where it is ({}, {})", settings.x, settings.y), settings.x, settings.y}};
            for (std::size_t index = 0; index < displays.size(); ++index) {
                if (index == selected || !wanted[index].enabled)
                    continue;
                for (const auto& [side, word] : {std::pair{desktop::Beside::left, "Left of"}, std::pair{desktop::Beside::right, "Right of"},
                                                 std::pair{desktop::Beside::above, "Above"}, std::pair{desktop::Beside::below, "Below"}}) {
                    const auto [x, y] = desktop::positionBeside(settings, wanted[index], side);
                    if (x != settings.x || y != settings.y)
                        places.push_back({std::format("{} {}", word, displays[index].name), x, y});
                }
            }
            std::vector<std::string> placeNames;
            for (const auto& place : places)
                placeNames.push_back(place.name);
            body->addChild(settingRow("Position", "where it sits beside your other displays", placeNames, 0, palette,
                                      [this, places, changed](std::size_t index) {
                                          wanted[selected].x = places[index].x;
                                          wanted[selected].y = places[index].y;
                                          changed();
                                      },
                                      defer));
        }
    }

    void DisplaysPage::fillSharpApps() {
        // Only where it matters: a display scaled by other than 100%, or the setting is on.
        const bool scaled = std::ranges::any_of(wanted, [](const auto& settings) { return settings.enabled && settings.scale != 1; });
        if (!sharpNow || (!scaled && !sharpNow->on))
            return;
        body->addChild(space(8));
        body->addChild(heading("Apps made for X11", palette));
        const auto sharp = desktop::sharpAppsFor(true, wanted);
        body->addChild(paragraph(std::format("Some older apps, such as Steam, Electron apps run without Wayland, and many games, run through XWayland. Scaled by Hyprland "
                                             "they look blurry. Kept sharp, they are drawn at the display's own pixels, so they look smaller unless they scale themselves{}.",
                                             sharp.gtkScale ? std::format("; GTK's apps are told to draw at {}×", *sharp.gtkScale) : ""),
                                 palette, true));
        body->addChild(settingRow("X11 apps", "for apps started after the change", {"Scaled, a little blurry", "Sharp, at their own size"}, sharpWanted ? 1 : 0, palette,
                                  [this](std::size_t index) {
                                      sharpWanted = index == 1;
                                      showOverview();
                                  },
                                  defer));
    }

    void DisplaysPage::fillStatus() {
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> buttons;
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> away;
        std::string                                              note;
        if (trying) {
            note = escapeMarkup(std::format("Keep these display settings? Putting them back in {} s.", secondsLeft));
            buttons.push_back(button("Put back", 120, false, [this] { defer([this] { putBack("Put back."); }); }));
            buttons.push_back(button("Keep", 120, true, [this] { defer([this] { keep(); }); }));
        } else {
            const auto planned = plan();
            if (configuration::latestChange(application::backupsFolder(foundation::currentEnvironment()), changeName))
                away.push_back(button("Undo last change", 160, false, [this] { defer([this] { undo(); }); }));
            if (!planned)
                note = escapeMarkup(std::format("Lantern cannot apply it: {}.", planned.error()));
            else if (!*planned)
                note = std::format("{}  {}", colored("✓", hex(palette->m_colors.accent)), escapeMarkup(outcome.empty() ? "Your displays are set like this." : outcome));
            else {
                buttons.push_back(button("Review changes", 160, true, [this] { defer([this] { showReview(); }); }));
                note = escapeMarkup(outcome);
            }
        }
        CSharedPointer<Hyprtoolkit::IElement> noteText;
        if (!note.empty()) {
            const auto made = markupLine(note, palette);
            made->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
            noteText = made;
        }
        setFooter(footer, buttons, noteText, away);
    }

    void DisplaysPage::showReview() {
        auto planned = plan();
        if (!planned || !*planned) {
            outcome = planned ? "" : planned.error();
            return showOverview();
        }
        reviewing = true;
        change    = std::move(**planned);
        outcome.clear();
        body->clearChildren();
        body->addChild(space(4));
        body->addChild(heading("Review: displays", palette));
        body->addChild(paragraph(std::format("Applying writes the change below, after keeping a copy of the config so it can be undone. Hyprland reads it at once. "
                                             "If you do not keep it within {} seconds, or Hyprland reports a problem with it, Lantern puts the config back.",
                                             trialSeconds),
                                 palette, true));
        setFooter(footer, {button("Cancel", 110, false, [this] { defer([this] { showOverview(); }); }), button("Apply", 110, true, [this] { defer([this] { apply(); }); })});
        diff.setRows(diffRows({change}, *palette));
        body->addChild(diff.element());
    }

    void DisplaysPage::apply() {
        if (!reviewing)
            return;
        reviewing         = false;
        const auto record = configuration::applyChanges({change}, application::backupsFolder(foundation::currentEnvironment()), changeName);
        if (!record) {
            outcome = std::format("Nothing was written: {}.", record.error());
            return showOverview();
        }
        trying      = *record;
        secondsLeft = trialSeconds;
        countDown(++trial);
        if (const auto program = hyprctl())
            application::checkHyprlandConfig(loop, *program, *record, [this, record = *record](std::optional<std::string> problem) {
                if (!problem || trying != record)
                    return;
                // Already put back by the check.
                trying.reset();
                outcome = *problem;
                refresh();
            });
        showOverview();
    }

    void DisplaysPage::countDown(unsigned which) {
        if (!trying || which != trial)
            return;
        if (secondsLeft <= 0)
            return putBack("Put back, since it was not kept.");
        fillStatus();
        --secondsLeft;
        loop.after(std::chrono::seconds{1}, [this, which] { countDown(which); });
    }

    void DisplaysPage::putBack(const std::string& why) {
        if (!trying)
            return;
        const auto record = *trying;
        trying.reset();
        ++trial;
        if (const auto undone = configuration::undoChange(record); !undone)
            outcome = std::format("It could not be put back: {}. Undo it from the config's backup.", undone.error());
        else
            outcome = why;
        // Hyprland reads the config again, then the displays are asked for once it has, and
        // what was tried is forgotten.
        displays.clear();
        showOverview();
        loop.after(std::chrono::milliseconds{1500}, [this] { refresh(); });
    }

    void DisplaysPage::keep() {
        trying.reset();
        ++trial;
        outcome = "Kept.";
        // Read again, the config's XWayland setting with them.
        displays.clear();
        refresh();
    }

    void DisplaysPage::undo() {
        const auto latest = configuration::latestChange(application::backupsFolder(foundation::currentEnvironment()), changeName);
        if (!latest)
            return showOverview();
        if (const auto undone = configuration::undoChange(*latest); !undone)
            outcome = std::format("Nothing was undone: {}.", undone.error());
        else
            outcome = "Undone.";
        displays.clear();
        showOverview();
        loop.after(std::chrono::milliseconds{1500}, [this] { refresh(); });
    }

    void DisplaysPage::embed() {
        root->removeChild(title);
        root->setMargin(0);
    }

}
