// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/idle-page.hpp"

#include "application/command.hpp"
#include "application/config-check.hpp"
#include "application/lantern-folder.hpp"
#include "configuration/autostart.hpp"
#include "configuration/lua-binds.hpp"
#include "configuration/main-config.hpp"
#include "desktop/clipboard.hpp"
#include "desktop/process.hpp"
#include "desktop/system.hpp"
#include "desktop/theme.hpp"
#include "desktop/wallpaper.hpp"
#include "ui/bar-preview.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"
#include "ui/widgets.hpp"

#include <hyprtoolkit/element/Image.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/Text.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <format>
#include <fstream>
#include <sstream>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr auto changeName = "Idle and lock";

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

        // "In 5 minutes" choices for a time, with the one the file has when it is none of them.
        std::vector<int> minuteChoices(std::vector<int> choices, int now) {
            if (std::ranges::find(choices, now) == choices.end()) {
                choices.push_back(now);
                std::ranges::sort(choices);
            }
            return choices;
        }

        // What the page needs and is missing, said the way every page says it.
        desktop::Requirement missing(std::string name, std::string purpose, std::string upstream, std::string package) {
            return desktop::Requirement{.name = std::move(name), .purpose = std::move(purpose), .upstream = std::move(upstream), .package = std::move(package), .found = std::nullopt};
        }

        // "SUPER + SHIFT + L" as people say it: "Super + Shift + L".
        std::string keysText(std::string_view keys) {
            if (keys == "XF86ScreenSaver")
                return "The keyboard's lock key";
            if (keys == "Scroll_Lock")
                return "Scroll Lock, or a lock button";
            std::string text{keys};
            for (const auto& [written, said] : {std::pair{"SUPER", "Super"}, std::pair{"SHIFT", "Shift"}, std::pair{"CTRL", "Ctrl"}, std::pair{"ALT", "Alt"}})
                if (const auto at = text.find(written); at != std::string::npos)
                    text.replace(at, std::string_view{written}.size(), said);
            return text;
        }

        // The palette the lock screen is drawn in: the desktop's theme, or the first one.
        const desktop::Palette& lockPalette() {
            const auto  config = configuration::mainConfig(configuration::lanternLaunch());
            const auto* theme  = config ? desktop::currentTheme(*config) : nullptr;
            return theme ? *theme : desktop::waybarPalettes().front();
        }

        std::string minutesText(int minutes) {
            if (minutes == 0)
                return "Never";
            if (minutes % 60 == 0)
                return std::format("After {} hour{}", minutes / 60, minutes == 60 ? "" : "s");
            return std::format("After {} minute{}", minutes, minutes == 1 ? "" : "s");
        }

    }

    IdlePage::IdlePage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop) :
        palette{std::move(palette)},
        defer{std::move(defer)},
        loop{std::move(loop)},
        diff{this->palette, this->defer,
             {RowList::Column{.width = 24, .monospace = true, .secondary = false, .markup = true, .heading = ""},
              RowList::Column{.width = 0, .monospace = true, .secondary = false, .markup = true, .heading = ""}},
             20} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        root->addChild(pageTitle("Idle and lock", this->palette));
        root->addChild(paragraph("What happens while the computer is left alone, the lock screen, and the power mode. hypridle watches for the time to pass and "
                                 "hyprlock draws the lock screen; Lantern writes their settings after you review them.",
                                 this->palette));
        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        root->addChild(body);
        footer = leftRow();
        root->addChild(footer);
        refresh();
    }

    void IdlePage::refresh() {
        if (reviewing)
            return;
        const auto environment = foundation::currentEnvironment();
        current                = desktop::idleIn(fileText(desktop::hypridleConfigFile(environment)).value_or(""));
        wanted                 = current;
        const auto lock        = fileText(desktop::hyprlockConfigFile(environment));
        lockNow                = !lock ? LockLook::none : desktop::lanternLockScreen(*lock) ? LockLook::lantern : LockLook::yours;
        // hyprlock has no look of its own: with no hyprlock.conf it does not start at all, so
        // Lantern's is the one to write.
        themedLock             = lockNow != LockLook::yours;
        designNow              = lock ? desktop::lockDesignIn(*lock) : std::nullopt;
        design                 = designNow.value_or(desktop::LockDesign{});
        const auto config      = configuration::mainConfig(configuration::lanternLaunch());
        hyprlandText           = config && config->lua() ? fileText(config->file).value_or("") : "";
        lockKeyNow             = desktop::lockKeyIn(hyprlandText);
        lockKey                = lockKeyNow;
        wallpaperImage         = desktop::shownWallpaperImage(environment);
        showOverview();

        powerModes.clear();
        if (const auto power = program("powerprofilesctl")) {
            (void)application::runCommand(loop, *power, {"list"}, [this](bool succeeded, std::string output) {
                if (!succeeded)
                    return;
                // "* balanced:" or "  performance:", the chosen one marked.
                std::istringstream lines{output};
                for (std::string line; std::getline(lines, line);) {
                    if (line.empty() || line.back() != ':')
                        continue;
                    const auto mode = line.substr(2, line.size() - 3);
                    powerModes.push_back(mode);
                    if (line.starts_with('*'))
                        powerMode = mode;
                }
                if (!reviewing)
                    showOverview();
            });
        }
        brightness.reset();
        if (const auto brightnessctl = program("brightnessctl"); brightnessctl && desktop::hasBacklight()) {
            // "gmux_backlight,backlight,500,50%,1000"
            (void)application::runCommand(loop, *brightnessctl, {"-m"}, [this](bool succeeded, std::string output) {
                const auto percent = output.find('%');
                const auto comma   = output.rfind(',', percent);
                int        value   = 0;
                if (!succeeded || percent == std::string::npos || comma == std::string::npos ||
                    std::from_chars(output.data() + comma + 1, output.data() + percent, value).ec != std::errc{})
                    return;
                brightness = value;
                if (!reviewing)
                    showOverview();
            });
        }
    }

    void IdlePage::restyle() {
        if (!reviewing)
            showOverview();
    }

    std::expected<std::vector<configuration::FileChange>, std::string> IdlePage::plan() const {
        const auto                             environment = foundation::currentEnvironment();
        std::vector<configuration::FileChange> planned;
        if (wanted != current) {
            auto idle = configuration::planEdit(desktop::hypridleConfigFile(environment), [this](std::string_view text) { return desktop::withIdle(text, wanted); });
            if (!idle)
                return std::unexpected{idle.error()};
            if (*idle)
                planned.push_back(std::move(**idle));
        }
        // In the Hyprland config: hypridle runs from when Hyprland starts, and the key that
        // locks at once.
        const bool startIdle = wanted != current && wanted != desktop::IdleSettings{};
        if (startIdle || lockKey != lockKeyNow) {
            const auto config = configuration::mainConfig(configuration::lanternLaunch());
            if (!config)
                return std::unexpected{config.error()};
            if (lockKey != lockKeyNow && !config->lua())
                return std::unexpected{"keys are set only in a Lua config (hyprland.lua) so far"};
            auto edited = configuration::planEdit(config->file, [this, startIdle, lua = config->lua()](std::string_view text) {
                auto result = startIdle ? configuration::withAutostart(text, lua, "hypridle", {}) : std::string{text};
                return lockKey != lockKeyNow ? desktop::withLockKey(result, lockKey) : result;
            });
            if (!edited)
                return std::unexpected{edited.error()};
            if (*edited)
                planned.push_back(std::move(**edited));
        }
        if (themedLock && (lockNow != LockLook::lantern || design != designNow)) {
            auto lock = configuration::planFile(desktop::hyprlockConfigFile(environment), desktop::lockScreen(lockPalette(), design, wallpaperImage));
            if (!lock)
                return std::unexpected{lock.error()};
            if (lock->changes())
                planned.push_back(std::move(*lock));
        }
        return planned;
    }

    void IdlePage::showOverview() {
        reviewing = false;
        body->clearChildren();
        // The timers, the lock screen and the power, each on a tab of its own.
        const auto tabs = leftRow();
        tabs->rebuild()->gap(4)->commence();
        for (const auto& [which, label] : {std::pair{Tab::idle, "When idle"}, std::pair{Tab::lock, "Lock screen"}, std::pair{Tab::power, "Power"}})
            tabs->addChild(button(label, 130, which == tab, [this, which] {
                defer([this, which] {
                    tab = which;
                    showOverview();
                });
            }));
        body->addChild(tabs);
        body->addChild(space(4));
        const auto columns = leftRow(0);
        // Both as tall as the page, so each starts at the top: a row centers what is shorter.
        columns->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->gap(24)->commence();
        const auto left  = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {0.5, 1.0}})->commence();
        const auto right = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {0.46, 1.0}})->commence();
        columns->addChild(left);
        columns->addChild(right);
        body->addChild(columns);
        switch (tab) {
            case Tab::idle: fillIdle(left, right); break;
            case Tab::lock: fillLock(left, right); break;
            case Tab::power: fillPower(left, right); break;
        }
        fillStatus();
    }

    void IdlePage::fillIdle(const CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column, const CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& side) {
        if (!program("hypridle"))
            side->addChild(requirement(missing("hypridle", "Watches for the time to pass and carries out what is chosen here. Lantern starts it once these are applied.",
                                                 "https://github.com/hyprwm/hypridle", "hypridle")));
        const auto timeRow = [this, &column](const std::string& name, const std::string& about, std::vector<int> choices, int desktop::IdleSettings::*setting) {
            const auto               shown = minuteChoices(std::move(choices), wanted.*setting);
            std::vector<std::string> items;
            for (const auto minutes : shown)
                items.push_back(minutesText(minutes));
            column->addChild(settingRow(name, about, std::move(items), static_cast<std::size_t>(std::ranges::find(shown, wanted.*setting) - shown.begin()), palette,
                                        [this, shown, setting](std::size_t chosen) {
                                            if (chosen < shown.size())
                                                wanted.*setting = shown[chosen];
                                            showOverview();
                                        },
                                        defer));
        };
        if (desktop::hasBacklight())
            timeRow(program("brightnessctl") ? "Dim the screen" : "Dim the screen (needs brightnessctl)", "to a tenth, back on the first touch", {0, 1, 2, 3, 5, 10},
                    &desktop::IdleSettings::dim);
        timeRow("Screensaver", "a clock, until a key or the mouse", {0, 1, 2, 3, 5, 10, 15, 30}, &desktop::IdleSettings::screensaver);
        timeRow("Lock", "the lock screen asks for your password", {0, 1, 2, 3, 5, 10, 15, 30, 60}, &desktop::IdleSettings::lock);
        if (wanted.screensaver > 0 && wanted.lock > 0 && wanted.lock <= wanted.screensaver)
            column->addChild(paragraph("The screen locks before the screensaver would start, so the clock asks for the password at once.", palette, true));
        else if (wanted.screensaver > 0)
            column->addChild(paragraph(wanted.lock > 0 ? std::format("The screensaver asks for the password once it has run for {} minute{}.", wanted.lock - wanted.screensaver,
                                                                     wanted.lock - wanted.screensaver == 1 ? "" : "s")
                                                       : "With Lock at Never, the screensaver never asks for the password.",
                                       palette, true));
        timeRow("Turn the screens off", "back on with a key or the mouse", {0, 1, 2, 3, 5, 10, 15, 30, 60}, &desktop::IdleSettings::screensOff);
        timeRow("Suspend", "the computer sleeps, using almost no power", {0, 10, 15, 30, 60, 120}, &desktop::IdleSettings::suspend);
        column->addChild(settingRow("Lock before sleeping", "so that it wakes up locked", {"No", "Yes"}, wanted.lockBeforeSleep ? 1 : 0, palette,
                                    [this](std::size_t chosen) {
                                        wanted.lockBeforeSleep = chosen == 1;
                                        showOverview();
                                    },
                                    defer));
    }

    void IdlePage::fillLock(const CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column, const CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& side) {
        const bool designing = themedLock;
        if (designing)
            side->addChild(lockPreview());
        // Only a hyprlock.conf of the user's own can be kept instead of Lantern's.
        std::vector<std::string> items{"The theme's colors"};
        if (lockNow == LockLook::yours)
            items.insert(items.begin(), "Yours, from hyprlock.conf");
        column->addChild(settingRow("Look", "Lantern's, or one of your own", std::move(items), lockNow == LockLook::yours && themedLock ? 1 : 0, palette,
                                    [this](std::size_t chosen) {
                                        themedLock = lockNow != LockLook::yours || chosen == 1;
                                        showOverview();
                                    },
                                    defer));
        if (lockNow == LockLook::none)
            column->addChild(paragraph("There is no hyprlock.conf yet, and without one hyprlock does not start, so the lock key and the screensaver do nothing. "
                                       "Applying writes this one.",
                                       palette, true));
        if (designing) {
            const auto choice = [this, &column]<typename Value>(const std::string& name, const std::string& about, std::vector<std::pair<std::string, Value>> options,
                                                                Value desktop::LockDesign::*member) {
                std::vector<std::string> labels;
                std::size_t              at = 0;
                for (const auto& [label, value] : options) {
                    if (value == design.*member)
                        at = labels.size();
                    labels.push_back(label);
                }
                column->addChild(settingRow(name, about, std::move(labels), at, palette,
                                            [this, options, member](std::size_t chosen) {
                                                if (chosen < options.size())
                                                    design.*member = options[chosen].second;
                                                themedLock = true;
                                                showOverview();
                                            },
                                            defer));
            };
            choice("Background", "what is behind the clock",
                   std::vector<std::pair<std::string, desktop::LockBackground>>{{"The blurred desktop", desktop::LockBackground::desktop},
                                                                                {"Your wallpaper", desktop::LockBackground::wallpaper},
                                                                                {"The theme's color", desktop::LockBackground::color}},
                   &desktop::LockDesign::background);
            choice("Clock", "the time, above the password", std::vector<std::pair<std::string, desktop::LockClock>>{{"Large", desktop::LockClock::large},
                                                                                                                   {"Small", desktop::LockClock::small},
                                                                                                                   {"None", desktop::LockClock::none}},
                   &desktop::LockDesign::clock);
            choice("Date", "under the clock", std::vector<std::pair<std::string, bool>>{{"Shown", true}, {"Hidden", false}}, &desktop::LockDesign::date);
            if (design.background == desktop::LockBackground::wallpaper && !wallpaperImage)
                column->addChild(paragraph("Lantern cannot tell which wallpaper is shown, so the lock screen uses the theme's color.", palette, true));
        }

        // A key that locks at once, from the keys offered and the one the config has.
        std::vector<std::string> keys{""};
        for (const auto offered : desktop::lockKeys())
            keys.emplace_back(offered);
        if (!lockKeyNow.empty() && std::ranges::none_of(keys, [this](const std::string& one) { return configuration::sameKeys(one, lockKeyNow); }))
            keys.push_back(lockKeyNow);
        std::vector<std::string> labels;
        std::size_t              at = 0;
        for (const auto& one : keys) {
            if (configuration::sameKeys(one, lockKey) || one == lockKey)
                at = labels.size();
            labels.push_back(one.empty() ? "None" : keysText(one));
        }
        column->addChild(settingRow("Lock key", "locks the screen at once", std::move(labels), at, palette,
                                    [this, keys](std::size_t chosen) {
                                        if (chosen < keys.size())
                                            lockKey = keys[chosen];
                                        showOverview();
                                    },
                                    defer));
        if (!lockKey.empty() && lockKey != lockKeyNow)
            for (const auto& bind : configuration::luaBindLines(hyprlandText))
                if (configuration::sameKeys(bind.keys, lockKey) && bind.command != std::optional<std::string>{"pidof hyprlock || hyprlock"}) {
                    column->addChild(paragraph(std::format("{} does something else now; this replaces it.", keysText(lockKey)), palette, true));
                    break;
                }

        if (const auto hyprlock = program("hyprlock")) {
            const auto row = leftRow(scaled(40.0));
            row->addChild(button("Lock now", 120, false, [hyprlock = *hyprlock] { (void)desktop::startDetached(hyprlock); }));
            row->addChild(markupLine("Your password unlocks it.", palette));
            side->addChild(row);
        } else
            side->addChild(requirement(missing("hyprlock", "Draws the lock screen and the screensaver. What is chosen here is written now and used once it is installed.",
                                                 "https://github.com/hyprwm/hyprlock", "hyprlock")));
    }

    CSharedPointer<Hyprtoolkit::IElement> IdlePage::requirement(const desktop::Requirement& needed) {
        return requirementBlock(needed, desktop::currentDistribution(), desktop::clipboardProgram(foundation::variable(foundation::currentEnvironment(), "PATH").value_or("")),
                                palette, defer, [this] { refresh(); });
    }

    CSharedPointer<Hyprtoolkit::IElement> IdlePage::lockPreview() const {
        const auto&  colors = lockPalette();
        const double height = scaled(260.0);
        const auto   size   = CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, height}};
        CSharedPointer<Hyprtoolkit::IElement> frame;
        CSharedPointer<Hyprtoolkit::IElement> inside;
        const auto                            column = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(4)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        if (design.background != desktop::LockBackground::color && wallpaperImage) {
            // The desktop blurred is drawn as the wallpaper under a darker veil.
            frame = Hyprtoolkit::CImageBuilder::begin()
                        ->path(wallpaperImage->string())
                        ->fitMode(Hyprtoolkit::IMAGE_FIT_MODE_COVER)
                        ->rounding(8)
                        ->size(CDynamicSize{size})
                        ->commence();
            const auto veil = Hyprtoolkit::CRectangleBuilder::begin()
                                  ->color([c = paletteColor(colors.base, design.background == desktop::LockBackground::desktop ? 0.55F : 0.2F)] { return c; })
                                  ->rounding(8)
                                  ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})
                                  ->commence();
            veil->addChild(column);
            frame->addChild(veil);
        } else {
            frame = Hyprtoolkit::CRectangleBuilder::begin()->color([c = paletteColor(colors.base)] { return c; })->rounding(8)->size(CDynamicSize{size})->commence();
            frame->addChild(column);
        }
        const auto text = [](const std::string& words, std::string_view color, float points) {
            return Hyprtoolkit::CTextBuilder::begin()
                ->text(std::string{words})
                ->fontSize(Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_ABSOLUTE, points})
                ->color([c = paletteColor(color)] { return c; })
                ->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})
                ->commence();
        };
        const auto now     = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::system_clock::now()}.get_local_time();
        const bool large   = design.clock == desktop::LockClock::large;
        const auto used    = (design.clock == desktop::LockClock::none ? 0.0 : large ? 44.0 : 26.0) + (design.date ? 18.0 : 0.0) + 30.0;
        column->addChild(space(std::max(0.0, (height - scaled(used)) / 2)));
        if (design.clock != desktop::LockClock::none)
            column->addChild(text(std::format("{:%H:%M}", std::chrono::floor<std::chrono::minutes>(now)), colors.text, scaled(large ? 34.0 : 18.0)));
        if (design.date) {
            const auto                    day = std::chrono::floor<std::chrono::days>(now);
            const std::chrono::year_month_day date{day};
            column->addChild(text(std::format("{:%A}, {:%B} {}", std::chrono::weekday{day}, date.month(), static_cast<unsigned>(date.day())), colors.subtle, scaled(9.0)));
        }
        column->addChild(space(scaled(6.0)));
        column->addChild(Hyprtoolkit::CRectangleBuilder::begin()
                             ->color([c = paletteColor(colors.surface)] { return c; })
                             ->borderColor([c = paletteColor(colors.accent)] { return c; })
                             ->borderThickness(1)
                             ->rounding(9)
                             ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {scaled(130.0), scaled(18.0)}})
                             ->commence());
        return frame;
    }

    void IdlePage::fillPower(const CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column, const CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& side) {
        if (!program("powerprofilesctl"))
            column->addChild(requirement(missing("power-profiles-daemon", "Switches between saving power, balanced and the most speed.",
                                                 "https://gitlab.freedesktop.org/upower/power-profiles-daemon", "power-profiles-daemon")));
        else if (powerModes.empty())
            column->addChild(paragraph("The power modes could not be read.", palette, true));
        else {
            const auto at = std::ranges::find(powerModes, powerMode);
            column->addChild(settingRow("Power mode", "saving power, or the most speed", powerModes, static_cast<std::size_t>(at - powerModes.begin()), palette,
                                        [this](std::size_t chosen) {
                                            if (chosen >= powerModes.size())
                                                return;
                                            if (const auto power = program("powerprofilesctl"))
                                                (void)application::runCommand(loop, *power, {"set", powerModes[chosen]}, [this](bool, std::string) { refresh(); });
                                        },
                                        defer));
        }
        if (!desktop::hasBacklight())
            return;
        if (!brightness) {
            if (program("brightnessctl"))
                column->addChild(paragraph("The brightness could not be read.", palette, true));
            else
                side->addChild(requirement(missing("brightnessctl", "Sets the built-in screen's brightness, and dims it when the computer is left alone.",
                                                     "https://github.com/Hummer12007/brightnessctl", "brightnessctl")));
            return;
        }
        std::vector<int> levels{10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
        if (std::ranges::find(levels, *brightness) == levels.end()) {
            levels.push_back(*brightness);
            std::ranges::sort(levels);
        }
        std::vector<std::string> items;
        for (const auto level : levels)
            items.push_back(std::format("{}%", level));
        column->addChild(settingRow("Brightness", "of the built-in screen", std::move(items), static_cast<std::size_t>(std::ranges::find(levels, *brightness) - levels.begin()),
                                    palette,
                                    [this, levels](std::size_t chosen) {
                                        if (chosen >= levels.size())
                                            return;
                                        if (const auto brightnessctl = program("brightnessctl"))
                                            (void)application::runCommand(loop, *brightnessctl, {"set", std::format("{}%", levels[chosen])}, [this](bool, std::string) { refresh(); });
                                    },
                                    defer));
    }

    void IdlePage::fillStatus() {
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

    void IdlePage::showReview() {
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
        body->addChild(heading("Review: idle and lock", palette));
        body->addChild(paragraph("Applying writes the changes below, after keeping a copy of each file so they can be undone, and starts hypridle again so that it "
                                 "reads them.",
                                 palette, true));
        setFooter(footer, {button("Cancel", 110, false, [this] { defer([this] { showOverview(); }); }), button("Apply", 110, true, [this] { defer([this] { apply(); }); })});
        diff.setRows(diffRows(changes, *palette));
        body->addChild(diff.element());
    }

    void IdlePage::apply() {
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
        if (const auto hyprctl = desktop::findProgram("hyprctl", foundation::variable(environment, "PATH").value_or("")))
            application::checkHyprlandConfig(loop, *hyprctl, *record, [this](std::optional<std::string> problem) {
                if (!problem)
                    return;
                outcome = *problem;
                if (!reviewing)
                    showOverview();
            });
        restartIdle();
        refresh();
    }

    void IdlePage::restartIdle() {
        for (const auto process : desktop::findProcesses("hypridle"))
            (void)desktop::stopProcess(process);
        const auto hypridle = program("hypridle");
        if (!hypridle || desktop::idleIn(fileText(desktop::hypridleConfigFile(foundation::currentEnvironment())).value_or("")) == desktop::IdleSettings{})
            return;
        // A moment for the old one to exit first.
        loop.after(std::chrono::milliseconds{300}, [hypridle = *hypridle] { (void)desktop::startDetached(hypridle); });
    }

    void IdlePage::undo() {
        const auto latest = configuration::latestChange(application::backupsFolder(foundation::currentEnvironment()), changeName);
        if (!latest)
            return showOverview();
        if (const auto undone = configuration::undoChange(*latest); !undone)
            outcome = std::format("Nothing was undone: {}.", undone.error());
        else {
            outcome = "Undone.";
            restartIdle();
        }
        refresh();
    }

}
