// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/windows-page.hpp"

#include "application/config-check.hpp"
#include "application/lantern-folder.hpp"
#include "configuration/main-config.hpp"
#include "desktop/system.hpp"
#include "desktop/theme.hpp"
#include "ui/bar-preview.hpp"
#include "ui/desktop-preview.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"
#include "ui/widgets.hpp"

#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/Text.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprtoolkit::CFontSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr auto   changeName    = "Windows";
        constexpr double previewHeight = 230;
        // The preview draws the desktop at a little over half its size.
        constexpr double previewScale  = 0.6;
        // Waybar's height, as the preview draws it.
        constexpr double barHeight     = 22;

        // The colors windows are drawn in: the desktop's theme, if Lantern set one, or
        // Lantern's own.
        struct Colors {
            std::string backdrop;
            std::string window;
            std::string content;
            std::string active;
            std::string inactive;
        };

        Colors colorsFor(const Hyprtoolkit::CPalette& studio) {
            if (const auto config = configuration::mainConfig(configuration::lanternLaunch()))
                if (const auto* theme = desktop::currentTheme(*config))
                    return {theme->dark ? "#3b4252" : "#b6bccb", std::string{theme->base}, std::string{theme->raised}, std::string{theme->accent}, std::string{theme->raised}};
            return {darkTheme(studio) ? "#3b4252" : "#b6bccb", hex(studio.m_colors.base), hex(studio.m_colors.alternateBase), hex(studio.m_colors.accent),
                    hex(studio.m_colors.alternateBase)};
        }

        CSharedPointer<Hyprtoolkit::CRectangleElement> block(const std::string& color, CDynamicSize size, double rounding = 0, const std::string& border = {}, int thickness = 0) {
            auto builder = Hyprtoolkit::CRectangleBuilder::begin()
                               ->color([c = paletteColor(color)] { return c; })
                               ->rounding(static_cast<int>(std::lround(rounding)))
                               ->size(std::move(size));
            if (thickness > 0)
                builder->borderColor([c = paletteColor(border)] { return c; })->borderThickness(thickness);
            return builder->commence();
        }

        CDynamicSize fixed(double width, double height) {
            return CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {width, height}};
        }

        CDynamicSize filling() {
            return CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}};
        }

        // A card's picture: what is drawn, centered up and down; a column centers it across.
        CSharedPointer<Hyprtoolkit::IElement> cardPicture(const std::string& background, const CSharedPointer<Hyprtoolkit::IElement>& inside, double used) {
            const auto frame  = block(background, CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, ChoiceCards::defaultPictureHeight}}, 6);
            const auto column = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(filling())->commence();
            column->addChild(space(std::max(0.0, (ChoiceCards::defaultPictureHeight - used) / 2)));
            column->addChild(inside);
            frame->addChild(column);
            return frame;
        }

    }

    WindowsPage::WindowsPage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop) :
        palette{std::move(palette)},
        defer{std::move(defer)},
        loop{std::move(loop)},
        cards{this->palette, this->defer, [this] { fillPreview(); }},
        diff{this->palette, this->defer,
             {RowList::Column{.width = 24, .monospace = true, .secondary = false, .markup = true, .heading = ""},
              RowList::Column{.width = 0, .monospace = true, .secondary = false, .markup = true, .heading = ""}},
             20},
        waybar{this->palette, this->defer, this->loop, "waybar", "Waybar", nullptr} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(filling())->commence();
        root->setMargin(28);
        root->addChild(pageTitle("Windows", this->palette));
        root->addChild(paragraph("How windows look and are handled: their corners, the gaps around them, their border, and whether dragging that border resizes "
                                 "them, and whether what shows through a see-through window is blurred. Holding Super and dragging with the right button resizes a window either way. Nothing is written until you review the change.",
                                 this->palette));
        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        root->addChild(body);
        footer = leftRow();
        root->addChild(footer);
        if (const auto config = configuration::mainConfig(configuration::lanternLaunch()))
            current = desktop::currentWindowLook(*config);
        wanted = current.value_or(desktop::WindowLook{});
        showOverview();
    }

    void WindowsPage::close() {
        waybar.close();
    }

    std::expected<std::vector<configuration::FileChange>, std::string> WindowsPage::plan() const {
        const auto config = configuration::mainConfig(configuration::lanternLaunch());
        if (!config)
            return std::unexpected{config.error()};
        const auto hyprland = desktop::planWindowLook(wanted, *config);
        if (!hyprland)
            return std::unexpected{hyprland.error()};
        auto planned = desktop::planBarFollowing(wanted, current, foundation::currentEnvironment());
        if (!planned)
            return std::unexpected{planned.error()};
        if (*hyprland)
            planned->insert(planned->begin(), **hyprland);
        return planned;
    }

    void WindowsPage::restyle() {
        cards.forget();
        if (!reviewing)
            showOverview();
    }

    void WindowsPage::showOverview() {
        reviewing = false;
        body->clearChildren();
        cards.clear();
        statusShown.clear();

        preview = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        body->addChild(preview);
        body->addChild(space(6));

        // Settings two to a row, so each pair lines up: a row centers what is shorter.
        std::vector<CSharedPointer<Hyprtoolkit::CColumnLayoutElement>> sides;
        for (int row = 0; row < 4; ++row) {
            const auto line = leftRow(0);
            line->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->gap(24)->commence();
            for (int side = 0; side < 2; ++side) {
                sides.push_back(Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {0.49, 1.0}})->commence());
                line->addChild(sides.back());
            }
            body->addChild(line);
        }
        // Row by row: corners and the gap between windows, the border and the screen's
        // edge, resizing and file pickers, then blur.
        const auto& corners = sides[0];
        const auto& between = sides[1];
        const auto& border  = sides[2];
        const auto& edge    = sides[3];
        const auto& resizing = sides[4];
        const auto& pickers  = sides[5];
        const auto& blurring = sides[6];

        const auto colors = colorsFor(*palette);
        // Presets, and the value the config has now when it is none of them, shown as
        // `shown` times the value: the distance it makes on screen.
        const auto choicesOf = [this](int desktop::WindowLook::*member, std::vector<std::pair<std::string, int>> presets, const std::function<CSharedPointer<Hyprtoolkit::IElement>(int)>& draw,
                                      const std::string& kind, int shown = 1) {
            if (current && std::ranges::none_of(presets, [&](const auto& preset) { return preset.second == (*current).*member; }))
                presets.emplace_back(std::format("Yours · {} px", (*current).*member * shown), (*current).*member);
            std::vector<ChoiceCards::Choice> made;
            for (const auto& [label, value] : presets)
                made.push_back(ChoiceCards::Choice{
                    .label  = label,
                    .chosen = [this, member, value] { return wanted.*member == value; },
                    .select = [this, member, value] { wanted.*member = value; },
                    .draw   = [this, draw, value, kind] { return cards.picture(std::format("{} {} {}", kind, value, wanted.rounding), [&] { return draw(value); }); },
                });
            return made;
        };

        cards.addSetting(corners, "Corners", "how round each window is",
                         choicesOf(&desktop::WindowLook::rounding, {{"Square", 0}, {"Slight", 4}, {"Round", 10}, {"Very round", 18}},
                                   [colors](int radius) {
                                       return cardPicture(colors.backdrop, block(colors.window, fixed(70, 38), radius * 0.8, colors.active, 2), 38);
                                   },
                                   "corners"));
        cards.addSetting(border, "Border", "the colored line around each window",
                         choicesOf(&desktop::WindowLook::borderSize, {{"None", 0}, {"Thin", 1}, {"Normal", 2}, {"Thick", 4}},
                                   // Drawn with the corners chosen, so the two read as one window.
                                   [this, colors](int border) {
                                       return cardPicture(colors.backdrop, block(colors.window, fixed(70, 38), wanted.rounding * 0.8, colors.active, border), 38);
                                   },
                                   "border"));
        std::vector<ChoiceCards::Choice> resize;
        for (const auto& [label, on] : {std::pair{"Off", false}, std::pair{"On", true}})
            resize.push_back(ChoiceCards::Choice{
                .label  = label,
                .chosen = [this, on] { return wanted.resizeOnBorder == on; },
                .select = [this, on] { wanted.resizeOnBorder = on; },
                .draw =
                    [this, on, colors] {
                        return cards.picture(std::format("resize {} {}", on, wanted.rounding), [&] {
                            // On, a window whose highlighted edge is being dragged out to the
                            // outline of the size it will be; off, a window and nothing more.
                            const auto radius  = wanted.rounding * 0.8;
                            const auto outline = block(colors.backdrop, fixed(80, 38), radius, colors.content, on ? 1 : 0);
                            const auto row     = Hyprtoolkit::CRowLayoutBuilder::begin()->gap(3)->size(filling())->commence();
                            row->addChild(block(colors.window, fixed(46, 38), radius, on ? colors.active : colors.inactive, on ? 3 : 1));
                            if (on) {
                                const auto arrow = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
                                arrow->addChild(space(8));
                                arrow->addChild(Hyprtoolkit::CTextBuilder::begin()
                                                    ->text("→")
                                                    ->fontSize(CFontSize{CFontSize::HT_FONT_ABSOLUTE, 14.F})
                                                    ->color([c = paletteColor(colors.active)] { return c; })
                                                    ->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})
                                                    ->commence());
                                row->addChild(arrow);
                            }
                            outline->addChild(row);
                            return cardPicture(colors.backdrop, outline, 38);
                        });
                    },
            });
        cards.addSetting(resizing, "Resize from the border", "drag a window's edge to resize it", std::move(resize));
        std::vector<ChoiceCards::Choice> picking;
        for (const auto& [label, floating] : {std::pair{"Tiled", false}, std::pair{"Floating", true}})
            picking.push_back(ChoiceCards::Choice{
                .label  = label,
                .chosen = [this, floating] { return wanted.floatFilePickers == floating; },
                .select = [this, floating] { wanted.floatFilePickers = floating; },
                .draw =
                    [this, floating, colors] {
                        return cards.picture(std::format("pickers {} {}", floating, wanted.rounding), [&] {
                            // Tiled, the dialog takes half the screen beside the app; floating,
                            // it sits in the middle, over the app.
                            const auto radius = wanted.rounding * 0.8;
                            if (floating) {
                                const auto app    = block(colors.window, fixed(84, 42), radius, colors.inactive, 1);
                                const auto middle = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(filling())->commence();
                                middle->setMargin(10);
                                middle->addChild(block(colors.content, filling(), radius * 0.5, colors.active, 2));
                                app->addChild(middle);
                                return cardPicture(colors.backdrop, app, 42);
                            }
                            const auto row = Hyprtoolkit::CRowLayoutBuilder::begin()->gap(4)->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
                            row->addChild(block(colors.window, fixed(40, 42), radius, colors.inactive, 1));
                            row->addChild(block(colors.window, fixed(40, 42), radius, colors.active, 2));
                            return cardPicture(colors.backdrop, row, 42);
                        });
                    },
            });
        cards.addSetting(pickers, "File pickers", "where open and save dialogs appear", std::move(picking));
        std::vector<ChoiceCards::Choice> blurs;
        for (const auto& [label, on] : {std::pair{"Off", false}, std::pair{"On", true}})
            blurs.push_back(ChoiceCards::Choice{
                .label  = label,
                .chosen = [this, on] { return wanted.blur == on; },
                .select = [this, on] { wanted.blur = on; },
                .draw =
                    [this, on, colors] {
                        return cards.picture(std::format("blur {} {}", on, wanted.rounding), [&] {
                            // A see-through window: off, the lines behind it show through
                            // sharp; on, they run together into a haze.
                            const auto radius = wanted.rounding * 0.8;
                            const auto window = block(colors.window, fixed(84, 42), radius, colors.active, 1);
                            const auto inside = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(5)->size(filling())->commence();
                            inside->setMargin(8);
                            if (on)
                                inside->addChild(Hyprtoolkit::CRectangleBuilder::begin()
                                                     ->color([c = paletteColor(colors.active, 0.35F)] { return c; })
                                                     ->rounding(10)
                                                     ->size(filling())
                                                     ->commence());
                            else
                                for (const double width : {60.0, 44.0, 52.0})
                                    inside->addChild(block(colors.active, fixed(width, 3), 1));
                            window->addChild(inside);
                            return cardPicture(colors.backdrop, window, 42);
                        });
                    },
            });
        cards.addSetting(blurring, "Blur", "behind see-through windows, such as a terminal", std::move(blurs));

        // Hyprland keeps its inner gap on each side of a window, so two windows are twice it
        // apart. The choices come in the screen edge's steps, so the same choice in both is
        // the same gap.
        cards.addSetting(between, "Between windows", "the distance between two windows",
                         choicesOf(&desktop::WindowLook::gapsIn, {{"None", 0}, {"Small", 5}, {"Normal", 10}, {"Wide", 20}},
                                   [colors](int gap) {
                                       // Two plain windows, and the gap between them drawn as a line as
                                       // thick as it is; with none they meet at a hairline.
                                       const auto row = Hyprtoolkit::CRowLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 38.0}})->commence();
                                       row->addChild(block(colors.window, fixed(36, 38), 3));
                                       if (gap == 0)
                                           row->addChild(block(colors.content, fixed(1, 38)));
                                       else
                                           row->addChild(block(colors.active, fixed(std::max(2.0, std::round(gap * 2 * 0.3)), 38), 1));
                                       row->addChild(block(colors.window, fixed(36, 38), 3));
                                       return cardPicture(colors.backdrop, row, 38);
                                   },
                                   "between", 2));
        cards.addSetting(edge, "Screen edge", "the gap between windows and the edges of the screen",
                         choicesOf(&desktop::WindowLook::gapsOut, {{"None", 0}, {"Small", 10}, {"Normal", 20}, {"Wide", 40}},
                                   [colors](int gap) {
                                       // A monitor on its stand, the gap between its edge and the window on
                                       // it highlighted.
                                       const auto bezel = block(colors.window, fixed(84, 42), 4);
                                       const auto glass = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(filling())->commence();
                                       glass->setMargin(3);
                                       const auto edgeGap = block(colors.active, filling(), 1);
                                       const auto inside  = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(filling())->commence();
                                       inside->setMargin(gap * 0.3);
                                       inside->addChild(block(colors.content, filling(), 2));
                                       edgeGap->addChild(inside);
                                       glass->addChild(edgeGap);
                                       bezel->addChild(glass);
                                       const auto monitor = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
                                       monitor->addChild(bezel);
                                       monitor->addChild(block(colors.window, fixed(10, 4)));
                                       monitor->addChild(block(colors.window, fixed(30, 3), 1));
                                       return cardPicture(colors.backdrop, monitor, 49);
                                   },
                                   "edge"));

        cards.update();
        fillPreview();
    }

    void WindowsPage::fillPreview() {
        preview->clearChildren();
        // The desktop in the theme's colors, so this and the Theme page show the same desktop,
        // with the bar following the windows as they are chosen here.
        const auto        config  = configuration::mainConfig(configuration::lanternLaunch());
        const auto*       theme   = config ? desktop::currentTheme(*config) : nullptr;
        const auto        current = desktop::currentBarStyle(foundation::currentEnvironment(), desktop::runningBarGeometry());
        const auto&       shown   = theme ? *theme : current ? *current->palette : desktop::waybarPalettes().front();
        auto              bar     = current.value_or(desktop::WaybarStyle{desktop::Shape::islands, &shown});
        bar.geometry              = desktop::barGeometry(wanted);
        preview->addChild(desktopPreview(shown, wanted, bar, previewScale, previewHeight));
        fillStatus();
    }

    void WindowsPage::fillStatus() {
        const auto environment = foundation::currentEnvironment();
        const auto planned     = plan();
        const bool undoable    = configuration::latestChange(application::backupsFolder(environment), changeName).has_value();
        const bool same        = planned && planned->empty();
        const auto shown       = std::format("{}|{}|{}|{}|{}|{}|{}|{}|{}|{}|{}", wanted.rounding, wanted.gapsIn, wanted.gapsOut, wanted.borderSize, wanted.resizeOnBorder,
                                             wanted.blur, wanted.floatFilePickers, same,
                                             planned ? "" : planned.error(), undoable, outcome);
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
            note = std::format("{}  {}", colored("✓", hex(palette->m_colors.accent)), escapeMarkup(outcome.empty() ? "Your windows look like this." : outcome));
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

    void WindowsPage::showReview() {
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
        body->addChild(heading("Review: windows", palette));
        const bool bar = std::ranges::any_of(changes, [](const auto& change) { return change.file.parent_path().filename() == "waybar"; });
        body->addChild(paragraph(std::format("Applying writes the changes below, after keeping a copy of each file so it can be undone. Hyprland reads its config at "
                                             "once; if it reports a problem with it, Lantern puts the config back.{}",
                                             bar ? " Waybar's corners and its distance from the edges follow the windows', so its files change too, and it restarts to "
                                                   "read them while it is on."
                                                 : ""),
                                 palette, true));
        setFooter(footer, {button("Cancel", 110, false, [this] { defer([this] { showOverview(); }); }), button("Apply", 110, true, [this] { defer([this] { apply(); }); })});
        diff.setRows(diffRows(changes, *palette));
        body->addChild(diff.element());
    }

    void WindowsPage::apply() {
        if (!reviewing)
            return;
        const auto environment = foundation::currentEnvironment();
        const auto record      = configuration::applyChanges(changes, application::backupsFolder(environment), changeName);
        if (!record) {
            outcome = std::format("Nothing was written: {}.", record.error());
            return showOverview();
        }
        outcome = "Applied.";
        if (std::ranges::any_of(changes, [](const auto& change) { return change.file.parent_path().filename() == "waybar"; }))
            waybar.restartToRead(*record, [this](std::string what) {
                if (!what.contains("put back"))
                    return;
                outcome = std::move(what);
                if (!reviewing)
                    showOverview();
            });
        if (const auto hyprctl = desktop::findProgram("hyprctl", foundation::variable(environment, "PATH").value_or("")))
            application::checkHyprlandConfig(loop, *hyprctl, *record, [this](std::optional<std::string> problem) {
                if (!problem)
                    return;
                outcome = *problem;
                if (const auto config = configuration::mainConfig(configuration::lanternLaunch()))
                    current = desktop::currentWindowLook(*config);
                if (!reviewing)
                    showOverview();
            });
        if (const auto config = configuration::mainConfig(configuration::lanternLaunch()))
            current = desktop::currentWindowLook(*config);
        showOverview();
    }

    void WindowsPage::undo() {
        const auto latest = configuration::latestChange(application::backupsFolder(foundation::currentEnvironment()), changeName);
        if (!latest)
            return showOverview();
        if (const auto undone = configuration::undoChange(*latest); !undone)
            outcome = std::format("Nothing was undone: {}.", undone.error());
        else
            outcome = "Undone.";
        if (const auto config = configuration::mainConfig(configuration::lanternLaunch()))
            current = desktop::currentWindowLook(*config);
        wanted = current.value_or(wanted);
        showOverview();
    }

}
