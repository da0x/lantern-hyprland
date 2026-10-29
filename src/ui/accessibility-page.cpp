// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/accessibility-page.hpp"

#include "application/config-check.hpp"
#include "application/settings.hpp"
#include "application/lantern-folder.hpp"
#include "configuration/main-config.hpp"
#include "desktop/process.hpp"
#include "desktop/system.hpp"
#include "ui/bar-preview.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"
#include "ui/widgets.hpp"

#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/Text.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprtoolkit::CFontSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr auto   changeName  = "Accessibility";
        // The record of a change keeps Lantern's own settings as they were, for Undo.
        constexpr auto   settingsKept  = "studio-settings";
        // Taller than other pages' pictures, so the largest cursor fits.
        constexpr double pictureHeight = 72;

        CSharedPointer<Hyprtoolkit::CRectangleElement> surface(std::string_view color, double height) {
            return Hyprtoolkit::CRectangleBuilder::begin()
                ->color([c = paletteColor(color)] { return c; })
                ->rounding(6)
                ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, height}})
                ->commence();
        }

        CSharedPointer<Hyprtoolkit::CTextElement> centeredText(const std::string& markup, float points, const std::string& color) {
            return Hyprtoolkit::CTextBuilder::begin()
                ->text(std::string{markup})
                ->fontSize(CFontSize{CFontSize::HT_FONT_ABSOLUTE, points})
                ->color([c = paletteColor(color)] { return c; })
                ->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})
                ->commence();
        }

        // A picture's content, centered up and down in the picture's height; a column centers
        // what is narrower than it across.
        CSharedPointer<Hyprtoolkit::IElement> centered(const CSharedPointer<Hyprtoolkit::IElement>& inside, double used) {
            const auto column = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, pictureHeight}})->commence();
            column->addChild(space(std::max(0.0, (pictureHeight - used) / 2)));
            column->addChild(inside);
            return column;
        }

        // Colors as a strip of small squares, on a background.
        CSharedPointer<Hyprtoolkit::IElement> chips(const std::string& background, const std::vector<std::string>& colors) {
            const auto frame = surface(background, pictureHeight);
            const auto row   = Hyprtoolkit::CRowLayoutBuilder::begin()->gap(5)->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 18.0}})->commence();
            for (const auto& color : colors)
                row->addChild(Hyprtoolkit::CRectangleBuilder::begin()
                                  ->color([c = paletteColor(color)] { return c; })
                                  ->rounding(4)
                                  ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {18.0, 18.0}})
                                  ->commence());
            frame->addChild(centered(row, 18));
            return frame;
        }

        // The bundled theme the terminal uses now, as designed, for the sample.
        const desktop::TerminalTheme* terminalInUse() {
            const auto  themes = desktop::terminalThemes();
            const auto* used   = desktop::currentTerminal(foundation::currentEnvironment());
            if (used != nullptr)
                if (const auto found = std::ranges::find(themes, used->name, &desktop::TerminalTheme::name); found != themes.end())
                    return &*found;
            return &themes.front();
        }

        // The cursor theme in use, whose size changes: the session's, or GTK's setting.
        std::string cursorTheme(const foundation::Environment& environment) {
            if (auto theme = foundation::variable(environment, "XCURSOR_THEME"))
                return *theme;
            if (const auto set = desktop::gsetting("org.gnome.desktop.interface", "cursor-theme", environment); set && set->size() > 2)
                return set->substr(1, set->size() - 2);
            return "Adwaita";
        }

        bool sameScale(double one, double other) {
            return std::abs(one - other) < 1e-6;
        }

        std::string describe(const desktop::Accessibility& settings) {
            std::vector<std::string> parts;
            parts.emplace_back(settings.contrast == desktop::Contrast::high ? "high contrast" : "standard contrast");
            if (settings.vision != desktop::Vision::typical)
                parts.push_back(std::format("colors for {} color blindness{}", desktop::nameOf(settings.vision), settings.wholeDesktop ? " across the screen" : ""));
            parts.push_back(std::format("text at {}%", std::lround(settings.textScale * 100)));
            parts.push_back(std::format("a {} px cursor", settings.cursorSize));
            if (settings.reduceMotion)
                parts.emplace_back("reduced motion");
            std::string text;
            for (std::size_t index = 0; index < parts.size(); ++index)
                text += std::format("{}{}", index == 0 ? "" : index + 1 == parts.size() ? " and " : ", ", parts[index]);
            return text;
        }

    }

    AccessibilityPage::AccessibilityPage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop) :
        palette{std::move(palette)},
        defer{std::move(defer)},
        loop{std::move(loop)},
        cards{this->palette, this->defer, [this] { fillStatus(); }, pictureHeight},
        diff{this->palette, this->defer,
             {RowList::Column{.width = 24, .monospace = true, .secondary = false, .markup = true, .heading = ""},
              RowList::Column{.width = 0, .monospace = true, .secondary = false, .markup = true, .heading = ""}},
             20} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        root->addChild(pageTitle("Accessibility", this->palette));
        root->addChild(paragraph("Make the desktop easier to see and to use. Each setting goes where the desktop's programs look for it: GTK apps and the "
                                 "desktop portal, Hyprland, apps built on hyprtoolkit, and every terminal theme. Nothing is written until you review the change.",
                                 this->palette));
        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        root->addChild(body);
        footer = leftRow();
        root->addChild(footer);
        current = inUse();
        wanted  = current;
        showOverview();
    }

    desktop::Accessibility AccessibilityPage::inUse() {
        const auto environment = foundation::currentEnvironment();
        const auto config      = configuration::mainConfig(configuration::lanternLaunch());
        auto       found       = config ? desktop::currentAccessibility(*config, environment) : desktop::Accessibility{};
        const auto settings    = application::loadSettings(application::lanternFolder(environment));
        if (const auto contrast = desktop::contrastNamed(settings.contrast))
            found.contrast = *contrast;
        // A shader in use says the color vision; without one, Lantern's record does.
        if (!found.wholeDesktop)
            if (const auto vision = desktop::visionNamed(settings.vision))
                found.vision = *vision;
        return found;
    }

    void AccessibilityPage::restyle() {
        cards.forget();
        if (!reviewing)
            showOverview();
    }

    void AccessibilityPage::showOverview() {
        reviewing = false;
        body->clearChildren();
        cards.clear();
        statusShown.clear();
        using desktop::Accessibility;

        const auto base  = terminalInUse();
        const auto index = static_cast<std::size_t>(base - desktop::terminalThemes().data());
        const auto themeFor = [index](desktop::Contrast contrast, desktop::Vision vision) { return &desktop::terminalThemes(contrast, vision)[index]; };
        const auto columns = leftRow(0);
        columns->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->gap(24)->commence();
        const auto left  = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {0.49, 1.0}})->commence();
        const auto right = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {0.49, 1.0}})->commence();
        columns->addChild(left);
        columns->addChild(right);
        body->addChild(columns);

        // Contrast: the terminal theme in use, at each.
        std::vector<ChoiceCards::Choice> contrast;
        for (const auto& [label, level] : {std::pair{"Standard", desktop::Contrast::standard}, std::pair{"High", desktop::Contrast::high}})
            contrast.push_back(ChoiceCards::Choice{
                .label  = label,
                .chosen = [this, level] { return wanted.contrast == level; },
                .select = [this, level] { wanted.contrast = level; },
                .draw =
                    [this, level, themeFor] {
                        const auto* theme = themeFor(level, wanted.vision);
                        return cards.picture(std::format("contrast {} {}", desktop::nameOf(level), desktop::nameOf(wanted.vision)), [&] {
                            const auto& color = theme->colors;
                            const auto  frame  = surface(theme->background, pictureHeight);
                            const auto  in     = [](const std::string& hex, std::string_view words) { return colored(words, hex); };
                            const auto  lines  = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(2)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
                            lines->addChild(centeredText(std::format("<span font_family=\"monospace\">{} {} {}</span>", in(color[1], "error"), in(color[2], "passed"), in(color[4], "info")), 9.F, theme->foreground));
                            lines->addChild(centeredText(std::format("<span font_family=\"monospace\">{} {}</span>", in(theme->foreground, "plain"), in(color[8], "# comment")), 9.F, theme->foreground));
                            frame->addChild(centered(lines, 32));
                            return frame;
                        });
                    },
            });
        cards.addSetting(left, "Contrast", "GTK's high-contrast style, and every terminal theme", std::move(contrast));

        // Color vision: the theme's six colors, as each makes them.
        std::vector<ChoiceCards::Choice> vision;
        for (const auto& [label, kind] : {std::pair{"Typical", desktop::Vision::typical}, std::pair{"Red-green", desktop::Vision::redGreen}, std::pair{"Blue-yellow", desktop::Vision::blueYellow}})
            vision.push_back(ChoiceCards::Choice{
                .label  = label,
                .chosen = [this, kind] { return wanted.vision == kind; },
                .select = [this, kind] { wanted.vision = kind; },
                .draw =
                    [this, kind, themeFor] {
                        const auto* theme = themeFor(wanted.contrast, kind);
                        return cards.picture(std::format("vision {} {}", desktop::nameOf(kind), desktop::nameOf(wanted.contrast)), [&] {
                            return chips(theme->background, {theme->colors[1], theme->colors[2], theme->colors[3], theme->colors[4], theme->colors[5], theme->colors[6]});
                        });
                    },
            });
        cards.addSetting(left, "Color vision", "colors told apart in every terminal theme", std::move(vision));

        // The whole screen: colors everyone knows, before and after the shader.
        const std::vector<std::string> reference{"#e53935", "#fb8c00", "#fdd835", "#43a047", "#1e88e5", "#8e24aa"};
        std::vector<ChoiceCards::Choice>            whole;
        for (const auto& [label, on] : {std::pair{"Off", false}, std::pair{"On", true}})
            whole.push_back(ChoiceCards::Choice{
                .label  = label,
                .chosen = [this, on] { return wanted.wholeDesktop == on; },
                .select = [this, on] { wanted.wholeDesktop = on; },
                .draw =
                    [this, on, reference] {
                        const auto kind = on ? wanted.vision : desktop::Vision::typical;
                        return cards.picture(std::format("whole {} {}", on, desktop::nameOf(kind)), [&] {
                            std::vector<std::string> shown;
                            for (const auto& color : reference)
                                shown.push_back(desktop::hexOf(desktop::corrected(desktop::parseColor(color), kind)));
                            return chips(hex(palette->m_colors.base), shown);
                        });
                    },
            });
        cards.addSetting(left, "Whole screen", "a Hyprland shader corrects every color, photos too", std::move(whole));

        // Text size: the same words at each size.
        std::vector<ChoiceCards::Choice> textSize;
        for (const double scale : {1.0, 1.25, 1.5, 1.75})
            textSize.push_back(ChoiceCards::Choice{
                .label  = std::format("{}%", std::lround(scale * 100)),
                .chosen = [this, scale] { return sameScale(wanted.textScale, scale); },
                .select = [this, scale] { wanted.textScale = scale; },
                .draw =
                    [this, scale] {
                        return cards.picture(std::format("text {}", scale), [&] {
                            const auto frame  = surface(hex(palette->m_colors.base), pictureHeight);
                            const auto points = static_cast<float>(11.0 * scale);
                            frame->addChild(centered(centeredText("Aa", points, hex(palette->m_colors.text)), points * 4.0 / 3.0 * 1.2));
                            return frame;
                        });
                    },
            });
        cards.addSetting(right, "Text size", "GTK apps, and hyprtoolkit's, Lantern among them", std::move(textSize));

        // Cursor size: an arrow at each size.
        std::vector<ChoiceCards::Choice> cursor;
        for (const int size : {24, 32, 48, 64})
            cursor.push_back(ChoiceCards::Choice{
                .label  = std::format("{} px", size),
                .chosen = [this, size] { return wanted.cursorSize == size; },
                .select = [this, size] { wanted.cursorSize = size; },
                .draw =
                    [this, size] {
                        return cards.picture(std::format("cursor {}", size), [&] {
                            const auto frame  = surface(hex(palette->m_colors.base), pictureHeight);
                            // The arrow's line is about a point and a half to the pixel, so the
                            // sizes keep their proportions and the largest fits its card.
                            const auto points = static_cast<float>(size) * 0.62F;
                            frame->addChild(centered(centeredText("🡴", points, hex(palette->m_colors.text)), points * 4.0 / 3.0 * 1.2));
                            return frame;
                        });
                    },
            });
        cards.addSetting(right, "Cursor size", "the pointer, in Hyprland and in apps", std::move(cursor));

        // Reduce motion: a window sliding in, with its trail, or standing still.
        std::vector<ChoiceCards::Choice> motion;
        for (const auto& [label, reduce] : {std::pair{"Animated", false}, std::pair{"Still", true}})
            motion.push_back(ChoiceCards::Choice{
                .label  = label,
                .chosen = [this, reduce] { return wanted.reduceMotion == reduce; },
                .select = [this, reduce] { wanted.reduceMotion = reduce; },
                .draw =
                    [this, reduce] {
                        return cards.picture(std::format("motion {}", reduce), [&] {
                            const auto frame = surface(hex(palette->m_colors.base), pictureHeight);
                            const auto row   = Hyprtoolkit::CRowLayoutBuilder::begin()->gap(3)->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 30.0}})->commence();
                            const auto window = [this](float alpha) {
                                return Hyprtoolkit::CRectangleBuilder::begin()
                                    ->color([palette = palette, alpha] { auto color = palette->m_colors.accent; color.a = alpha; return color; })
                                    ->rounding(4)
                                    ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {40.0, 30.0}})
                                    ->commence();
                            };
                            if (!reduce) {
                                row->addChild(window(0.15F));
                                row->addChild(window(0.35F));
                            }
                            row->addChild(window(1.0F));
                            frame->addChild(centered(row, 30));
                            return frame;
                        });
                    },
            });
        cards.addSetting(right, "Motion", "Hyprland's animations, and apps asked to keep still", std::move(motion));

        cards.update();
        fillStatus();
    }

    void AccessibilityPage::fillStatus() {
        const auto environment = foundation::currentEnvironment();
        const auto config      = configuration::mainConfig(configuration::lanternLaunch());
        const auto planned     = config ? desktop::planAccessibility(wanted, *config, environment)
                                        : std::expected<desktop::AccessibilityPlan, std::string>{std::unexpect, config.error()};
        const bool undoable    = configuration::latestChange(application::backupsFolder(environment), changeName).has_value();
        const bool same        = planned && planned->empty() && wanted == current;
        const auto shown       = std::format("{}|{}|{}|{}|{}", describe(wanted), same, planned ? "" : planned.error(), undoable, outcome);
        if (shown == statusShown)
            return;
        statusShown = shown;

        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> buttons;
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> away;
        if (undoable)
            away.push_back(button("Undo last change", 160, false, [this] { defer([this] { undo(); }); }));
        std::string note;
        if (!planned) {
            note = escapeMarkup(std::format("Lantern cannot apply it: {}.", planned.error()));
        } else if (same) {
            note = std::format("{}  {}", colored("✓", hex(palette->m_colors.accent)), escapeMarkup(outcome.empty() ? "Your desktop uses these settings." : outcome));
        } else {
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

    void AccessibilityPage::showReview() {
        const auto environment = foundation::currentEnvironment();
        const auto config      = configuration::mainConfig(configuration::lanternLaunch());
        auto       planned     = config ? desktop::planAccessibility(wanted, *config, environment)
                                        : std::expected<desktop::AccessibilityPlan, std::string>{std::unexpect, config.error()};
        if (!planned) {
            outcome = planned.error();
            return showOverview();
        }
        reviewing = true;
        plan      = std::move(*planned);
        outcome.clear();
        body->clearChildren();
        body->addChild(space(4));
        body->addChild(heading(std::format("Review: {}", describe(wanted)), palette));
        body->addChild(paragraph("Applying will:", palette));
        std::vector<std::string> steps;
        if (!plan.files.empty())
            steps.emplace_back("Write the changes below. A copy of each file is kept first, so it can be undone.");
        for (const auto& setting : plan.settings)
            steps.push_back(std::format("Set {} in {} from {} to {}, for GTK apps and the desktop portal.", setting.key, setting.schema, setting.current, setting.wanted));
        if (wanted.contrast != current.contrast || wanted.vision != current.vision)
            steps.emplace_back("Show every terminal theme in this version. The Terminal page then offers your theme's new version to review.");
        if (wanted.cursorSize != current.cursorSize)
            steps.emplace_back("Resize Hyprland's cursor at once.");
        for (const auto& step : steps)
            body->addChild(paragraph(std::format("•  {}", step), palette, true));
        setFooter(footer, {button("Cancel", 110, false, [this] { defer([this] { showOverview(); }); }), button("Apply", 110, true, [this] { defer([this] { apply(); }); })});
        if (!plan.files.empty()) {
            diff.setRows(diffRows(plan.files, *palette));
            body->addChild(diff.element());
        }
    }

    void AccessibilityPage::apply() {
        if (!reviewing)
            return;
        const auto environment = foundation::currentEnvironment();
        const auto record      = configuration::applyChanges(plan.files, application::backupsFolder(environment), std::format("{}: {}", changeName, describe(wanted)));
        if (!record) {
            outcome = std::format("Nothing was written: {}.", record.error());
            return showOverview();
        }
        desktop::setGsettings(plan.settings, *record, environment);
        // Lantern's own record of contrast and color vision, kept as it was for Undo.
        const auto folder   = application::lanternFolder(environment);
        auto       settings = application::loadSettings(folder);
        std::ofstream{*record / settingsKept} << "contrast\t" << settings.contrast << "\nvision\t" << settings.vision << '\n';
        settings.contrast = std::string{desktop::nameOf(wanted.contrast)};
        settings.vision   = std::string{desktop::nameOf(wanted.vision)};
        (void)application::saveSettings(settings, folder);

        const auto path    = foundation::variable(environment, "PATH").value_or("");
        const auto hyprctl = desktop::findProgram("hyprctl", path);
        // The theme Hyprland uses stays; only its size changes.
        if (hyprctl && wanted.cursorSize != current.cursorSize)
            (void)desktop::readCommand(*hyprctl, {"setcursor", cursorTheme(environment), std::to_string(wanted.cursorSize)});
        outcome = std::format("Applied {}.", describe(wanted));
        const bool config = std::ranges::any_of(plan.files, [](const auto& change) { return change.file.extension() == ".lua"; });
        if (config && hyprctl)
            application::checkHyprlandConfig(loop, *hyprctl, *record, [this](std::optional<std::string> problem) {
                if (!problem)
                    return;
                outcome = *problem;
                current = inUse();
                if (!reviewing)
                    showOverview();
            });
        current = inUse();
        wanted  = current;
        if (applied)
            applied(current);
        showOverview();
    }

    void AccessibilityPage::undo() {
        const auto environment = foundation::currentEnvironment();
        const auto latest      = configuration::latestChange(application::backupsFolder(environment), changeName);
        if (!latest)
            return showOverview();
        if (const auto undone = configuration::undoChange(*latest); !undone) {
            outcome = std::format("Nothing was undone: {}.", undone.error());
            return showOverview();
        }
        desktop::restoreGsettings(*latest, environment);
        const auto folder   = application::lanternFolder(environment);
        auto       settings = application::loadSettings(folder);
        std::ifstream kept{*latest / settingsKept};
        for (std::string line; std::getline(kept, line);) {
            const auto tab = line.find('\t');
            if (tab == std::string::npos)
                continue;
            (line.starts_with("contrast") ? settings.contrast : settings.vision) = line.substr(tab + 1);
        }
        (void)application::saveSettings(settings, folder);
        const auto before = current;
        current           = inUse();
        wanted            = current;
        if (const auto hyprctl = desktop::findProgram("hyprctl", foundation::variable(environment, "PATH").value_or("")); hyprctl && before.cursorSize != current.cursorSize)
            (void)desktop::readCommand(*hyprctl, {"setcursor", cursorTheme(environment), std::to_string(current.cursorSize)});
        outcome = "Undone.";
        if (applied)
            applied(current);
        showOverview();
    }

}
