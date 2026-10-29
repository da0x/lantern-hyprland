// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/bar-page.hpp"
#include "desktop/user-themes.hpp"

#include "application/config-check.hpp"
#include "application/settings.hpp"
#include "configuration/main-config.hpp"
#include "application/lantern-folder.hpp"
#include "configuration/diff.hpp"
#include "application/lantern-folder.hpp"
#include "configuration/diff.hpp"
#include "desktop/clipboard.hpp"
#include "desktop/idle.hpp"
#include "desktop/popups.hpp"
#include "desktop/process.hpp"
#include "desktop/waybar.hpp"
#include "desktop/window-look.hpp"
#include "ui/bar-preview.hpp"
#include "ui/widgets.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"

#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/Combobox.hpp>
#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <optional>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprtoolkit::CFontSize;
        using Hyprutils::Memory::CSharedPointer;

        // Three cards to a row, with the gaps between them.
        constexpr double cardsPerRow = 3;
        constexpr double cardShare   = 0.32;

        constexpr auto popupsChange = "Pop-ups";

        std::string fileText(const std::filesystem::path& file) {
            std::ifstream     stream{file, std::ios::binary};
            std::stringstream text;
            text << stream.rdbuf();
            return text.str();
        }

        std::optional<std::filesystem::path> program(std::string_view name) {
            return desktop::findProgram(name, foundation::variable(foundation::currentEnvironment(), "PATH").value_or(""));
        }

        // The style the pop-ups are written in: the one swayosd has now, when Lantern wrote
        // it or there is none.
        std::expected<std::optional<configuration::FileChange>, std::string> planPopupStyle(const desktop::Palette& palette) {
            const auto file = desktop::popupStyleFile(foundation::currentEnvironment());
            if (std::error_code error; std::filesystem::exists(file, error) && !desktop::lanternPopupStyle(fileText(file)))
                return std::nullopt;
            auto planned = configuration::planFile(file, desktop::popupStyle(palette));
            if (!planned)
                return std::unexpected{planned.error()};
            if (!planned->changes())
                return std::nullopt;
            return std::optional{std::move(*planned)};
        }

        const desktop::ShapeInfo& shapeInfo(desktop::Shape shape) {
            for (const auto& info : desktop::waybarShapes())
                if (info.shape == shape)
                    return info;
            return desktop::waybarShapes().front();
        }

    }

    BarPage::BarPage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop) :
        palette{std::move(palette)},
        defer{std::move(defer)},
        loop{std::move(loop)},
        diff{this->palette, this->defer,
             {RowList::Column{.width = 24, .monospace = true, .secondary = false, .markup = true, .heading = ""},
              RowList::Column{.width = 0, .monospace = true, .secondary = false, .markup = true, .heading = ""}},
             20},
        waybar{this->palette, this->defer, this->loop, "waybar", "Waybar", nullptr} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        root->addChild(pageTitle("Bar", this->palette));
        root->addChild(paragraph("Lantern has no bar of its own. It recommends Waybar, the bar most Hyprland desktops use, and helps you set it up. "
                                 "It never installs anything: when something is missing, it shows the command for you to run.",
                                 this->palette));

        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        root->addChild(waybar.element());
        root->addChild(body);
        footer = leftRow();
        root->addChild(footer);

        const auto environment = foundation::currentEnvironment();
        settings               = application::loadSettings(application::lanternFolder(environment));
        selected               = desktop::currentBarStyle(environment, desktop::runningBarGeometry())
                                     .value_or(desktop::WaybarStyle{desktop::Shape::islands, &desktop::waybarPalettes().front(), desktop::runningBarGeometry()});
        // The thin line is no longer offered: a bar that has it is shown with the inactive
        // windows' border, ready to review.
        if (selected.border == desktop::Border::thin)
            selected.border = desktop::Border::inactive;
        check();
    }

    void BarPage::close() {
        waybar.close();
    }

    void BarPage::restyle() {
        if (!reviewing && !reviewingPopups)
            showOverview();
    }

    void BarPage::check() {
        waybar.refresh();
        const auto config = configuration::mainConfig(configuration::lanternLaunch());
        popupsNow         = config && config->lua() && desktop::popupsIn(fileText(config->file));
        popupsWanted      = popupsNow;
        numpadNow         = config && config->lua() && desktop::numpadVolumeIn(fileText(config->file));
        numpadWanted      = numpadNow;
        if (!reviewing && !reviewingPopups)
            showOverview();
    }

    BarPage::Card BarPage::card(const std::string& name, std::function<void()> chosenNow, bool nameFirst) {
        Card made;
        made.title = name;
        made.frame = Hyprtoolkit::CRectangleBuilder::begin()
                         ->color([palette = palette] { return palette->m_colors.alternateBase; })
                         ->rounding(palette->m_vars.bigRounding)
                         ->borderColor([] { return Hyprtoolkit::CHyprColor{0, 0, 0, 0}; })
                         ->borderThickness(2)
                         ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {cardShare, 1.0}})
                         ->commence();
        const auto inside = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(8)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        inside->setMargin(10);
        made.holder = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        made.name   = markupLine(escapeMarkup(name), palette);
        const CSharedPointer<Hyprtoolkit::IElement> title = made.name;
        const CSharedPointer<Hyprtoolkit::IElement> held = made.holder;
        inside->addChild(nameFirst ? title : held);
        inside->addChild(nameFirst ? held : title);
        made.frame->addChild(inside);
        made.frame->setReceivesMouse(true);
        made.frame->setMouseButton([this, chosenNow = std::move(chosenNow)](Hyprtoolkit::Input::eMouseButton button, bool pressed) {
            if (button == Hyprtoolkit::Input::MOUSE_BUTTON_LEFT && pressed)
                defer(chosenNow);
        });
        return made;
    }

    CSharedPointer<Hyprtoolkit::IElement> BarPage::previewOf(int kind, const desktop::WaybarStyle& style) {
        constexpr double cardScale = 0.42;
        // The cards show the bar with only the standard widgets, as more do not fit at their
        // size; the large one shows them all.
        auto shown = style;
        if (kind != 0)
            shown.widgets = desktop::standardWidgets();
        auto& made = previews[{kind, shown.shape, shown.palette, shown.border, shown.widgets}];
        if (!made)
            made = barPreview(shown, kind == 0 ? 1.0 : cardScale);
        return made;
    }

    CSharedPointer<Hyprtoolkit::IElement> BarPage::styleSection() {
        const auto section = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();

        // The chosen style at Waybar's own size; what applying it would do is in the footer.
        largePreview = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        styleStatusShown.clear();
        section->addChild(largePreview);

        // The look of the bar, and what is on it, each on a tab of its own.
        const auto tabs = leftRow();
        tabs->rebuild()->gap(4)->commence();
        tabButtons.clear();
        for (const auto& [which, label] : {std::pair{Tab::style, "Style"}, std::pair{Tab::widgets, "Widgets"}, std::pair{Tab::popups, "Pop-ups"}}) {
            auto made = button(label, 100, which == tab, [this, which] {
                defer([this, which] {
                    tab = which;
                    fillChoices();
                    updateStyle();
                });
            });
            tabs->addChild(made);
            tabButtons.push_back(std::move(made));
        }
        section->addChild(space(4));
        section->addChild(tabs);
        choices = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        section->addChild(choices);
        fillChoices();
        updateStyle();
        return section;
    }

    void BarPage::fillChoices() {
        choices->clearChildren();
        shapeCards.clear();
        paletteCards.clear();
        borderCards.clear();
        for (std::size_t index = 0; index < tabButtons.size(); ++index)
            tabButtons[index]->rebuild()->accent(index == static_cast<std::size_t>(tab))->commence();
        if (tab == Tab::popups)
            return fillPopups();
        if (tab == Tab::style) {
            choices->addChild(markupLine("<span weight=\"600\">Shape</span>", palette));
            const auto shapes = leftRow(0);
            shapes->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->gap(12)->commence();
            shapeCards.clear();
            for (const auto& info : desktop::waybarShapes()) {
                auto made = card(std::string{info.name}, [this, shape = info.shape] {
                    selected.shape = shape;
                    updateStyle();
                });
                shapes->addChild(made.frame);
                shapeCards.emplace_back(info.shape, std::move(made));
            }
            choices->addChild(shapes);

            choices->addChild(markupLine("<span weight=\"600\">Colors</span>", palette));
            const auto     all    = desktop::allPalettes();
            constexpr auto perRow = static_cast<std::size_t>(cardsPerRow);
            paletteCards.clear();
            for (std::size_t first = 0; first < all.size(); first += perRow) {
                const auto colors = leftRow(0);
                colors->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->gap(12)->commence();
                for (std::size_t index = first; index < std::min(all.size(), first + perRow); ++index) {
                    auto made = card(std::string{all[index]->name}, [this, chosen = all[index]] {
                        selected.palette = chosen;
                        updateStyle();
                    });
                    colors->addChild(made.frame);
                    paletteCards.emplace_back(all[index], std::move(made));
                }
                choices->addChild(colors);
            }

            choices->addChild(markupLine("<span weight=\"600\">Border</span>", palette));
            const auto borders = leftRow(0);
            borders->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->gap(12)->commence();
            for (const auto& [border, name] : {std::pair{desktop::Border::inactive, "Dim"}, std::pair{desktop::Border::active, "Lit"}}) {
                auto made = card(name, [this, border] {
                    selected.border = border;
                    updateStyle();
                });
                borders->addChild(made.frame);
                borderCards.emplace_back(border, std::move(made));
            }
            choices->addChild(borders);
            return;
        }

        // A column for each side of the bar, its widgets in the order they are shown, each with
        // a button that takes it off; under them, a choice of the widgets not on the bar, which
        // adds one at the end.
        std::error_code error;
        bool            battery = false;
        for (const auto& supply : std::filesystem::directory_iterator{"/sys/class/power_supply", error})
            battery = battery || supply.path().filename().string().starts_with("BAT");
        const auto infoOf = [](desktop::Widget widget) -> const desktop::WidgetInfo& {
            return *std::ranges::find(desktop::waybarWidgets(), widget, &desktop::WidgetInfo::widget);
        };
        std::vector<desktop::Widget> off;
        for (const auto& info : desktop::waybarWidgets())
            if (!desktop::sideOf(selected.widgets, info.widget))
                off.push_back(info.widget);
        // All three as tall as the longest, since a row centers shorter children.
        std::size_t longest = 0;
        for (std::size_t side = 0; side < selected.widgets.size(); ++side)
            longest = std::max(longest, selected.widgets[side].size());
        // A tile: the widget's name, and under it the widget as the bar draws it, both centered.
        // The previews shrink when a column holds many, so the page still fits its window: a
        // page laid out taller than that comes out wrong.
        constexpr double tilesHeight = 440;
        const double     each        = tilesHeight / static_cast<double>(std::max<std::size_t>(longest, 1)) - 6;
        const double     widgetScale = std::clamp((each - scaled(18.0) - 14) / 38, 0.35, 0.7);
        const double     rowHeight   = scaled(18.0) + 38 * widgetScale + 14;
        const double     height      = scaled(34.0) + static_cast<double>(longest) * (rowHeight + 6) + 24;
        const auto tile = [this, &infoOf, rowHeight, widgetScale](desktop::Widget widget) {
            const auto made = Hyprtoolkit::CRectangleBuilder::begin()
                                  ->color([palette = palette] { return palette->m_colors.base; })
                                  ->rounding(palette->m_vars.smallRounding)
                                  ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, rowHeight}})
                                  ->commence();
            // A column centers what is narrower than itself.
            const auto stack = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(4)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
            stack->setMargin(5);
            const auto name = markupLine(escapeMarkup(std::string{infoOf(widget).name}), palette, CFontSize{CFontSize::HT_FONT_SMALL}, 0.8F);
            name->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
            stack->addChild(name);
            stack->addChild(widgetPreview(selected, widget, widgetScale));
            made->addChild(stack);
            return made;
        };

        const auto table = leftRow(0);
        table->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->gap(12)->commence();
        constexpr std::array<std::string_view, 3> titles{"Left", "Center", "Right"};
        for (std::size_t column = 0; column < titles.size(); ++column) {
            const auto side = static_cast<desktop::Side>(column);
            const auto area = Hyprtoolkit::CRectangleBuilder::begin()
                                  ->color([palette = palette] { return palette->m_colors.alternateBase; })
                                  ->rounding(palette->m_vars.bigRounding)
                                  ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {0.325, height}})
                                  ->commence();
            const auto inside = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
            inside->setMargin(8);
            // The column's name, and a button that lists the widgets it can add.
            const auto top = leftRow(34);
            top->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, scaled(34.0)}})->commence();
            const auto title = markupLine(std::format("<span weight=\"600\">{}</span>", titles[column]), palette);
            title->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
            title->setGrow(true, false);
            top->addChild(title);
            if (!off.empty())
                top->addChild(button("+", 34, false, [this, side, off] { defer([this, side, off] { openAddMenu(side, off); }); }));
            inside->addChild(top);
            for (const auto widget : selected.widgets[column]) {
                const auto& info = infoOf(widget);
                // What it shows, and what it would show nothing without on this computer.
                std::string summary{info.summary};
                if (widget == desktop::Widget::bluetooth && !std::filesystem::exists("/usr/lib/bluetooth/bluetoothd"))
                    summary = "Needs bluez, which is not installed.";
                if (widget == desktop::Widget::battery && !battery)
                    summary = "This computer reports no battery.";
                const auto row = tile(widget);
                row->setTooltip(std::move(summary));
                // The button that takes it off, at the tile's right edge.
                const auto remove = button("\u2212", 30, false, [this, widget] {
                    defer([this, widget] {
                        desktop::removeWidget(selected.widgets, widget);
                        fillChoices();
                        updateStyle();
                    });
                });
                remove->setPositionMode(Hyprtoolkit::IElement::HT_POSITION_ABSOLUTE);
                remove->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_RIGHT, true);
                remove->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_VCENTER, true);
                remove->setAbsolutePosition({-6, 0});
                row->addChild(remove);
                // Up and down along the column, beside it, for a widget that can go there.
                const auto& list  = selected.widgets[column];
                const auto  place = std::ranges::find(list, widget) - list.begin();
                double      right = -40;
                for (const auto& [label, by] : {std::pair{"\u2193", 1}, std::pair{"\u2191", -1}}) {
                    const bool can = by < 0 ? place > 0 : place + 1 < static_cast<std::ptrdiff_t>(list.size());
                    if (!can) {
                        right -= 34;
                        continue;
                    }
                    const auto move = button(label, 30, false, [this, widget, by] {
                        defer([this, widget, by] {
                            desktop::moveWidget(selected.widgets, widget, by);
                            fillChoices();
                            updateStyle();
                        });
                    });
                    move->setPositionMode(Hyprtoolkit::IElement::HT_POSITION_ABSOLUTE);
                    move->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_RIGHT, true);
                    move->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_VCENTER, true);
                    move->setAbsolutePosition({right, 0.0});
                    row->addChild(move);
                    right -= 34;
                }
                inside->addChild(row);
            }
            area->addChild(inside);
            table->addChild(area);
        }
        choices->addChild(table);
    }

    void BarPage::setWindow(CSharedPointer<Hyprtoolkit::IWindow> window) {
        this->window = std::move(window);
    }

    void BarPage::openAddMenu(desktop::Side side, std::vector<desktop::Widget> off) {
        if (menu)
            menu->close();
        menu.reset();
        if (!window || off.empty())
            return;
        // A menu of the widgets not on the bar, at the pointer, as a dropdown opens its list.
        constexpr double itemHeight = 32;
        const double     height     = std::min(600.0, static_cast<double>(off.size()) * (itemHeight + 4) + 12);
        menu = Hyprtoolkit::CWindowBuilder::begin()
                   ->type(Hyprtoolkit::HT_WINDOW_POPUP)
                   ->parent(window)
                   ->pos(window->cursorPos())
                   ->preferredSize(Hyprutils::Math::Vector2D{200.0, height})
                   ->commence();
        if (!menu)
            return;
        menuClosed = menu->m_events.popupClosed.listen([this] { menu.reset(); });
        const auto background = Hyprtoolkit::CRectangleBuilder::begin()
                                    ->color([palette = palette] { return palette->m_colors.background; })
                                    ->rounding(palette->m_vars.bigRounding)
                                    ->borderColor([palette = palette] { return palette->m_colors.alternateBase; })
                                    ->borderThickness(1)
                                    ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})
                                    ->commence();
        const auto items = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(4)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        items->setMargin(6);
        for (const auto widget : off) {
            const auto& info = *std::ranges::find(desktop::waybarWidgets(), widget, &desktop::WidgetInfo::widget);
            items->addChild(Hyprtoolkit::CButtonBuilder::begin()
                                ->label(std::string{info.name})
                                ->noBorder(true)
                                ->noBg(true)
                                ->alignText(Hyprtoolkit::HT_FONT_ALIGN_LEFT)
                                ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, itemHeight}})
                                ->onMainClick([this, side, widget](CSharedPointer<Hyprtoolkit::CButtonElement>) {
                                    defer([this, side, widget] {
                                        if (menu)
                                            menu->close();
                                        menu.reset();
                                        desktop::placeWidget(selected.widgets, widget, side);
                                        fillChoices();
                                        updateStyle();
                                    });
                                })
                                ->commence());
        }
        background->addChild(items);
        menu->m_rootElement = background;
        menu->open();
    }

    void BarPage::updateStyle() {
        largePreview->clearChildren();
        largePreview->addChild(previewOf(0, selected));
        const auto mark = [this](Card& made, bool chosen, const desktop::WaybarStyle& shown, int kind) {
            made.holder->clearChildren();
            made.holder->addChild(previewOf(kind, shown));
            made.frame->rebuild()->borderColor([palette = palette, chosen] { return chosen ? palette->m_colors.accent : Hyprtoolkit::CHyprColor{0, 0, 0, 0}; })->commence();
            made.name->rebuild()->text(chosen ? std::format("<span weight=\"600\">{}</span>", escapeMarkup(made.title)) : escapeMarkup(made.title))->commence();
        };
        for (auto& [shape, made] : shapeCards)
            mark(made, shape == selected.shape, desktop::WaybarStyle{shape, selected.palette, selected.geometry, selected.border, selected.widgets}, 1);
        for (auto& [shown, made] : paletteCards)
            mark(made, shown == selected.palette, desktop::WaybarStyle{selected.shape, shown, selected.geometry, selected.border, selected.widgets}, 2);
        for (auto& [border, made] : borderCards)
            mark(made, border == selected.border, desktop::WaybarStyle{selected.shape, selected.palette, selected.geometry, border, selected.widgets}, 3);
        fillStyleStatus();
    }

    std::expected<std::vector<configuration::FileChange>, std::string> BarPage::plan(const desktop::WaybarStyle& style) {
        auto planned = desktop::planWaybarStyle(style, foundation::currentEnvironment());
        if (!planned)
            return planned;
        std::erase_if(*planned, [](const auto& change) { return !change.changes(); });
        // The pop-ups take the bar's colors, when Lantern wrote their style.
        if (std::error_code error; style.palette && std::filesystem::exists(desktop::popupStyleFile(foundation::currentEnvironment()), error)) {
            auto popups = planPopupStyle(*style.palette);
            if (!popups)
                return std::unexpected{popups.error()};
            if (*popups)
                planned->push_back(std::move(**popups));
        }
        if (const auto config = configuration::mainConfig(configuration::lanternLaunch())) {
            auto rules = desktop::planPopupRules(*config);
            if (!rules)
                return std::unexpected{rules.error()};
            if (*rules)
                planned->push_back(std::move(**rules));
        }
        return planned;
    }

    void BarPage::fillStyleStatus() {
        if (tab == Tab::popups)
            return fillPopupStatus();
        // The windows' geometry, which the bar follows, may have changed on the Windows page.
        selected.geometry = desktop::runningBarGeometry();
        const auto environment = foundation::currentEnvironment();
        const auto planned     = plan(selected);
        const bool undoable    = configuration::latestChange(application::backupsFolder(environment), "Waybar").has_value();
        const bool same        = planned && std::ranges::none_of(*planned, [](const auto& change) { return change.changes(); });
        const auto shown       = std::format("{}|{}|{}|{}|{}", planned ? "" : planned.error(), same ? selected.palette->name : "", same ? shapeInfo(selected.shape).name : "", undoable,
                                             outcome);
        if (shown == styleStatusShown)
            return;
        styleStatusShown = shown;

        // Everything said here sits in the footer, so that saying it moves nothing above.
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> buttons;
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> away;
        if (undoable)
            away.push_back(button("Undo last change", 160, false, [this] { defer([this] { undo(); }); }));
        std::string note;
        if (!planned) {
            note = escapeMarkup(std::format("Lantern cannot write it: {}.", planned.error()));
        } else if (same) {
            note = std::format("{}  Your Waybar uses {}, {}.", colored("✓", hex(palette->m_colors.accent)), escapeMarkup(selected.palette->name),
                               escapeMarkup(std::string{shapeInfo(selected.shape).name}));
        } else {
            buttons.push_back(button("Review changes", 160, true, [this] { defer([this] { showReview(selected); }); }));
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

    void BarPage::showOverview() {
        reviewing.reset();
        reviewingPopups = false;
        changes.clear();
        body->clearChildren();

        const auto environment  = foundation::currentEnvironment();
        const auto distribution = desktop::currentDistribution();
        const auto copier       = desktop::clipboardProgram(foundation::variable(environment, "PATH").value_or(""));
        const auto requirements = desktop::waybarRequirements(environment);
        const bool allMet       = std::ranges::all_of(requirements, [](const auto& requirement) { return requirement.met(); });

        // Once everything is installed and the user dismissed the list, it stays hidden,
        // until something goes missing again.
        if (!allMet || !settings.barRequirementsDismissed) {
            const auto checks = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(18)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
            for (const auto& requirement : requirements)
                checks->addChild(requirementBlock(requirement, distribution, copier, palette, defer, [this] { check(); }));

            const auto config = desktop::waybarConfig(environment);
            std::string reads;
            if (!config)
                reads = "Waybar has no config to read, so it will not start.";
            else if (config->string().starts_with("/etc/"))
                reads = std::format("Waybar reads its example config, {}. It was written for Sway, so it shows none of Hyprland's workspaces.", config->string());
            else
                reads = std::format("Waybar reads {}.", shortPath(*config));
            checks->addChild(paragraph(reads, palette));

            body->addChild(space(4));
            body->addChild(checks);
            const auto actions = leftRow();
            if (allMet)
                actions->addChild(button("Dismiss", 110, false, [this] {
                    defer([this] {
                        settings.barRequirementsDismissed = true;
                        if (const auto saved = application::saveSettings(settings, application::lanternFolder(foundation::currentEnvironment())); !saved)
                            outcome = std::format("Lantern could not save its settings: {}.", saved.error());
                        showOverview();
                    });
                }));
            body->addChild(actions);
            body->addChild(space(8));
        }
        body->addChild(styleSection());
    }

    void BarPage::showReview(const desktop::WaybarStyle& style) {
        auto planned = plan(style);
        if (!planned) {
            outcome = planned.error();
            return showOverview();
        }
        reviewing = style;
        changes   = std::move(*planned);
        outcome.clear();
        body->clearChildren();

        body->addChild(space(4));
        body->addChild(heading(std::format("Review: {}, {}", style.palette->name, shapeInfo(style.shape).name), palette));
        const bool off = !waybar.running();
        const bool rules = std::ranges::any_of(changes, [](const auto& change) { return change.file.extension() == ".lua"; });
        body->addChild(paragraph(std::format("Applying writes these files. A copy of each file it replaces is kept first, so it can be undone. {}{}",
                                             off ? "Waybar is off, so it will not be restarted." : "Waybar then restarts to read them.",
                                             rules ? " The Hyprland config gets rules that float the volume control and the network settings the bar opens, under its right end." : ""),
                                 palette, true));
        if (desktop::sideOf(style.widgets, desktop::Widget::weather))
            body->addChild(paragraph("The weather widget asks the website wttr.in for the temperature every half hour. The site sees your computer's address, "
                                     "which tells it roughly where you are.",
                                     palette, true));
        setFooter(footer, {button("Cancel", 110, false, [this] { defer([this] { showOverview(); }); }), button("Apply", 110, true, [this] { defer([this] { apply(); }); })});
        diff.setRows(diffRows(changes, *palette));
        body->addChild(diff.element());
    }

    void BarPage::apply() {
        if (!reviewing)
            return;
        const auto record = configuration::applyChanges(changes, application::backupsFolder(foundation::currentEnvironment()), std::format("Waybar style: {}, {}", reviewing->palette->name, shapeInfo(reviewing->shape).name));
        if (!record) {
            outcome = std::format("Nothing was written: {}.", record.error());
        } else {
            if (std::ranges::any_of(changes, [](const auto& change) { return change.file == desktop::popupStyleFile(foundation::currentEnvironment()); }))
                restartPopups();
            outcome = "Written. Waybar is restarting…";
            waybar.restartToRead(*record, [this](std::string what) {
                outcome = std::move(what);
                if (!reviewing)
                    showOverview();
            });
            // Hyprland reads the rules by itself; a mistake in them is undone.
            const auto hyprctl = desktop::findProgram("hyprctl", foundation::variable(foundation::currentEnvironment(), "PATH").value_or(""));
            if (hyprctl && std::ranges::any_of(changes, [](const auto& change) { return change.file.extension() == ".lua"; }))
                application::checkHyprlandConfig(loop, *hyprctl, *record, [this](std::optional<std::string> problem) {
                    if (!problem)
                        return;
                    outcome = *problem;
                    if (!reviewing)
                        showOverview();
                });
        }
        showOverview();
    }

    void BarPage::undo() {
        const auto latest = configuration::latestChange(application::backupsFolder(foundation::currentEnvironment()), "Waybar");
        if (!latest)
            return showOverview();
        if (const auto undone = configuration::undoChange(*latest); !undone) {
            outcome = std::format("Nothing was undone: {}.", undone.error());
        } else if (waybar.running()) {
            outcome = "Undone, and Waybar is restarting to read the files as they were.";
            waybar.restart();
        } else {
            outcome = "Undone.";
        }
        showOverview();
    }

    std::expected<std::vector<configuration::FileChange>, std::string> BarPage::planPopups() const {
        std::vector<configuration::FileChange> planned;
        if (popupsWanted == popupsNow && (!popupsWanted || numpadWanted == numpadNow))
            return planned;
        const auto config = configuration::mainConfig(configuration::lanternLaunch());
        if (!config)
            return std::unexpected{config.error()};
        if (!config->lua())
            return std::unexpected{"keys are set only in a Lua config (hyprland.lua) so far"};
        auto edited = configuration::planEdit(config->file, [on = popupsWanted, numpad = numpadWanted](std::string_view text) {
            return desktop::withPopups(text, on, desktop::hasBacklight(), numpad);
        });
        if (!edited)
            return std::unexpected{edited.error()};
        if (*edited)
            planned.push_back(std::move(**edited));
        if (popupsWanted) {
            // In the colors of the bar Waybar has, or else of the one chosen here.
            const auto current = desktop::currentBarStyle(foundation::currentEnvironment(), desktop::runningBarGeometry());
            const auto colors = current && current->palette ? current->palette : selected.palette;
            if (colors) {
                auto style = planPopupStyle(*colors);
                if (!style)
                    return std::unexpected{style.error()};
                if (*style)
                    planned.push_back(std::move(**style));
            }
        }
        return planned;
    }

    void BarPage::fillPopups() {
        choices->addChild(paragraph("A pop-up shows the new level when you press a volume or brightness key, in the bar's colors. swayosd draws it.", palette));
        choices->addChild(settingRow("Pop-ups", "for the volume and brightness keys", {"Off", "On"}, popupsWanted ? 1 : 0, palette,
                                     [this](std::size_t chosen) {
                                         popupsWanted = chosen == 1;
                                         fillChoices();
                                         updateStyle();
                                     },
                                     defer));
        choices->addChild(paragraph(std::format("On, the volume and mute keys{} ask swayosd to change the level and show it. Off puts back what Hyprland's example "
                                                "config has them do, with wpctl and brightnessctl.",
                                                desktop::hasBacklight() ? " and the brightness keys" : ""),
                                    palette, true));
        if (popupsWanted)
            choices->addChild(settingRow("Number pad", "Super with its + and − changes the volume too", {"Off", "On"}, numpadWanted ? 1 : 0, palette,
                                         [this](std::size_t chosen) {
                                             numpadWanted = chosen == 1;
                                             fillChoices();
                                             updateStyle();
                                         },
                                         defer));
        if (popupsWanted && !program("swayosd-server"))
            choices->addChild(requirementBlock(desktop::Requirement{.name     = "swayosd",
                                                                    .purpose  = "Draws the volume and brightness pop-ups.",
                                                                    .upstream = "https://github.com/ErikReider/SwayOSD",
                                                                    .package  = "swayosd",
                                                                    .found    = std::nullopt},
                                               desktop::currentDistribution(),
                                               desktop::clipboardProgram(foundation::variable(foundation::currentEnvironment(), "PATH").value_or("")), palette, defer, [this] {
                                                   fillChoices();
                                                   updateStyle();
                                               }));
    }

    void BarPage::fillPopupStatus() {
        styleStatusShown.clear();
        const auto                                               planned = planPopups();
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> buttons;
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> away;
        if (configuration::latestChange(application::backupsFolder(foundation::currentEnvironment()), popupsChange))
            away.push_back(button("Undo last change", 160, false, [this] { defer([this] { undoPopups(); }); }));
        std::string note;
        if (!planned)
            note = escapeMarkup(std::format("Lantern cannot write it: {}.", planned.error()));
        else if (planned->empty())
            note = std::format("{}  {}", colored("✓", hex(palette->m_colors.accent)), popupsNow ? "The pop-ups are on." : "The pop-ups are off.");
        else {
            buttons.push_back(button("Review changes", 160, true, [this] { defer([this] { showPopupReview(); }); }));
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

    void BarPage::showPopupReview() {
        auto planned = planPopups();
        if (!planned || planned->empty()) {
            outcome = planned ? "" : planned.error();
            return showOverview();
        }
        changes = std::move(*planned);
        outcome.clear();
        body->clearChildren();
        reviewingPopups = true;
        body->addChild(space(4));
        body->addChild(heading(popupsWanted ? "Review: pop-ups on" : "Review: pop-ups off", palette));
        body->addChild(paragraph(std::format("Applying writes these files, after keeping a copy of each so they can be undone, and {}.",
                                             popupsWanted ? "starts swayosd's server" : "stops swayosd's server"),
                                 palette, true));
        setFooter(footer, {button("Cancel", 110, false, [this] { defer([this] { showOverview(); }); }), button("Apply", 110, true, [this] { defer([this] { applyPopups(); }); })});
        diff.setRows(diffRows(changes, *palette));
        body->addChild(diff.element());
    }

    void BarPage::applyPopups() {
        if (!reviewingPopups)
            return;
        reviewingPopups   = false;
        const auto record = configuration::applyChanges(changes, application::backupsFolder(foundation::currentEnvironment()), popupsChange);
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
                check();
            });
        check();
        restartPopups();
    }

    void BarPage::undoPopups() {
        const auto latest = configuration::latestChange(application::backupsFolder(foundation::currentEnvironment()), popupsChange);
        if (!latest)
            return showOverview();
        if (const auto undone = configuration::undoChange(*latest); !undone)
            outcome = std::format("Nothing was undone: {}.", undone.error());
        else
            outcome = "Undone.";
        check();
        restartPopups();
    }

    void BarPage::restartPopups() {
        for (const auto process : desktop::findProcesses("swayosd-server"))
            (void)desktop::stopProcess(process);
        const auto server = program("swayosd-server");
        if (!server || !popupsNow)
            return;
        // A moment for the old one to exit first.
        loop.after(std::chrono::milliseconds{300}, [server = *server] { (void)desktop::startDetached(server); });
    }

}
