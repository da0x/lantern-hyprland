// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/live-tree-page.hpp"

#include "ui/bar-preview.hpp"
#include "ui/markup.hpp"
#include "ui/restyle.hpp"
#include "ui/texts.hpp"
#include "ui/widgets.hpp"

#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprtoolkit::CFontSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr double rowHeight    = 30;
        constexpr double detailsWidth = 420;
        // The map's largest size; the monitors are drawn as large as fits in it.
        constexpr double mapHeight    = 260;
        constexpr double mapWidth     = 900;

        CDynamicSize fixed(double width, double height) {
            return CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {width, height}};
        }

        // Placed `at` from its parent's top left corner, rather than laid out.
        void place(const CSharedPointer<Hyprtoolkit::IElement>& element, double x, double y) {
            element->setPositionMode(Hyprtoolkit::IElement::HT_POSITION_ABSOLUTE);
            element->setAbsolutePosition({x, y});
        }

        CSharedPointer<Hyprtoolkit::CRectangleElement> box(Hyprtoolkit::CHyprColor fill, CDynamicSize size, int rounding, Hyprtoolkit::CHyprColor border, int thickness) {
            auto builder = Hyprtoolkit::CRectangleBuilder::begin()->color([fill] { return fill; })->rounding(rounding)->size(std::move(size));
            if (thickness > 0)
                builder->borderColor([border] { return border; })->borderThickness(thickness);
            return builder->commence();
        }

        CSharedPointer<Hyprtoolkit::CTextElement> label(const std::string& markup, const Hyprtoolkit::CHyprColor& color, double width) {
            return Hyprtoolkit::CTextBuilder::begin()
                ->text(std::string{markup})
                ->fontSize(CFontSize{CFontSize::HT_FONT_SMALL})
                ->color([color] { return color; })
                ->align(Hyprtoolkit::HT_FONT_ALIGN_LEFT)
                ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {width, 1.0}})
                ->commence();
        }

        // Its size in the layout: its pixels divided by its scale, turned when it is.
        std::pair<double, double> layoutSize(const ipc::Monitor& monitor) {
            const auto scale = monitor.scale > 0 ? monitor.scale : 1.0;
            return {monitor.width / scale, monitor.height / scale};
        }

        std::string workspaceName(const ipc::Workspace& workspace) {
            if (workspace.id < 0)
                return std::format("Special workspace {}", workspace.name.starts_with("special:") ? workspace.name.substr(8) : workspace.name);
            return workspace.name == std::to_string(workspace.id) ? std::format("Workspace {}", workspace.id) : std::format("Workspace {} ({})", workspace.id, workspace.name);
        }

    }

    LiveTreePage::LiveTreePage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer) :
        palette{std::move(palette)},
        defer{defer},
        list{this->palette, defer,
             {RowList::Column{.width = 0, .monospace = false, .secondary = false, .markup = true, .heading = ""},
              RowList::Column{.width = detailsWidth, .monospace = false, .secondary = true, .markup = true, .heading = ""}},
             rowHeight} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(8)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        title = pageTitle("Live tree", this->palette);
        root->addChild(title);
        status = paragraph("Connecting to Hyprland.", this->palette);
        root->addChild(status);
        map = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, mapHeight}})->commence();
        root->addChild(map);
        const auto body = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        body->addChild(list.element());
        root->addChild(body);
        list.setClicked([this](std::size_t index) {
            if (index >= rowWindows.size() || rowWindows[index].empty())
                return;
            selected = selected == rowWindows[index] ? std::nullopt : std::optional{rowWindows[index]};
            this->defer([this] {
                showMap();
                showRows();
            });
        });
    }

    void LiveTreePage::showConnection(application::Connection connection, const std::string& reason) {
        this->connection = connection;
        connectionReason = reason;
        updateStatus();
    }

    void LiveTreePage::showTree(const std::expected<ipc::Tree, std::string>& tree) {
        if (!tree) {
            treeError = tree.error();
            updateStatus();
            return;
        }
        treeError.clear();
        const bool same = received && *tree == this->tree;
        received        = true;
        this->tree      = *tree;
        updateStatus();
        if (same)
            return;
        showMap();
        showRows();
    }

    void LiveTreePage::restyle() {
        showMap();
        showRows();
    }

    void LiveTreePage::updateStatus() {
        std::string text;
        switch (connection) {
            case application::Connection::connecting: text = "Connecting to Hyprland."; break;
            case application::Connection::live:
                text = received ? std::format("{} {}, {} {} and {} {}, live from Hyprland. A window dragged or resized by hand shows where it was until something else changes.",
                                              tree.monitors.size(), tree.monitors.size() == 1 ? "monitor" : "monitors", tree.workspaces.size(),
                                              tree.workspaces.size() == 1 ? "workspace" : "workspaces", tree.windows.size(), tree.windows.size() == 1 ? "window" : "windows")
                                : "Reading the monitors, workspaces and windows from Hyprland.";
                break;
            case application::Connection::lost:
                text = std::format("Not connected to Hyprland: {}. Trying again; what is below may be out of date.", connectionReason);
                break;
        }
        if (!treeError.empty())
            text += std::format(" Could not read them: {}.", treeError);
        status->setText(escapeMarkup(text));
    }

    void LiveTreePage::showMap() {
        map->clearChildren();
        if (tree.monitors.empty())
            return;
        // The layout's bounds, so the monitors keep their places relative to each other.
        double left = std::numeric_limits<double>::max(), top = left, right = std::numeric_limits<double>::lowest(), bottom = right;
        for (const auto& monitor : tree.monitors) {
            const auto [width, height] = layoutSize(monitor);
            left                       = std::min(left, static_cast<double>(monitor.x));
            top                        = std::min(top, static_cast<double>(monitor.y));
            right                      = std::max(right, monitor.x + width);
            bottom                     = std::max(bottom, monitor.y + height);
        }
        const auto scale = std::min(scaled(mapWidth) / std::max(1.0, right - left), mapHeight / std::max(1.0, bottom - top));

        const auto& colors  = palette->m_colors;
        const auto  canvas   = Hyprtoolkit::CNullBuilder::begin()->size(fixed((right - left) * scale, (bottom - top) * scale))->commence();
        const auto  backdrop = darkTheme(*palette) ? paletteColor("#1d2021") : paletteColor("#c9cdd6");
        for (const auto& monitor : tree.monitors) {
            const auto [width, height] = layoutSize(monitor);
            const auto screen = box(backdrop, fixed(width * scale, height * scale), 6, monitor.focused ? colors.accent : colors.alternateBase, 2);
            place(screen, (monitor.x - left) * scale, (monitor.y - top) * scale);

            // The windows on what it shows, tiled first and floating over them, the special
            // workspace over both.
            std::vector<const ipc::Window*> shown;
            for (const auto& window : tree.windows)
                if (!window.hidden && (window.workspace == monitor.activeWorkspace || (monitor.specialWorkspace != 0 && window.workspace == monitor.specialWorkspace)))
                    shown.push_back(&window);
            std::ranges::stable_sort(shown, {}, [&monitor](const ipc::Window* window) { return std::pair{window->workspace == monitor.specialWorkspace, window->floating}; });
            for (const auto* window : shown) {
                const bool picked  = selected == window->address;
                const bool focused = window->focusOrder == 0;
                const auto drawn   = box(colors.alternateBase, fixed(std::max(4.0, window->width * scale), std::max(4.0, window->height * scale)), 4,
                                         picked || focused ? colors.accent : colors.base, picked ? 3 : focused ? 2 : 1);
                place(drawn, (window->x - monitor.x) * scale, (window->y - monitor.y) * scale);
                if (window->width * scale > 50 && window->height * scale > 22) {
                    const auto name = label(escapeMarkup(window->windowClass.empty() ? window->title : window->windowClass), colors.text, window->width * scale - 12);
                    place(name, 6, 4);
                    drawn->addChild(name);
                }
                screen->addChild(drawn);
            }
            const auto name = label(std::format("<span weight=\"600\">{}</span>  {}", escapeMarkup(monitor.name), monitor.activeWorkspace), colors.text, width * scale - 12);
            place(name, 6, height * scale - 20);
            screen->addChild(name);
            canvas->addChild(screen);
        }
        map->addChild(canvas);
    }

    void LiveTreePage::showRows() {
        std::vector<RowList::Row> rows;
        rowWindows.clear();
        const auto accent = hex(palette->m_colors.accent);
        const auto add    = [&](std::string tree, std::string details, std::string window, bool marked) {
            rows.push_back(RowList::Row{.cells = {std::move(tree), std::move(details)}, .marked = marked});
            rowWindows.push_back(std::move(window));
        };
        const auto windowsOf = [this, &add, &accent](const ipc::Workspace& workspace) {
            std::vector<const ipc::Window*> windows;
            for (const auto& window : tree.windows)
                if (window.workspace == workspace.id)
                    windows.push_back(&window);
            std::ranges::sort(windows, {}, [](const ipc::Window* window) { return std::pair{window->y, window->x}; });
            for (const auto* window : windows) {
                std::vector<std::string> facts{window->floating ? "floating" : "tiled", std::format("{}×{} at {},{}", window->width, window->height, window->x, window->y)};
                if (window->fullscreen)
                    facts.emplace_back("fullscreen");
                if (window->pinned)
                    facts.emplace_back("pinned");
                if (window->xwayland)
                    facts.emplace_back("XWayland");
                if (window->hidden)
                    facts.emplace_back("hidden");
                std::string details;
                for (const auto& fact : facts)
                    details += (details.empty() ? "" : " · ") + fact;
                const auto mark = window->focusOrder == 0 ? colored("●", accent) + " " : std::string{"   "};
                add(std::format("            {}<span weight=\"600\">{}</span>  <span alpha=\"70%\">{}</span>", mark, escapeMarkup(window->windowClass), escapeMarkup(window->title)),
                    escapeMarkup(details), window->address, selected == window->address);
            }
        };
        for (const auto& monitor : tree.monitors) {
            add(std::format("<span weight=\"600\">{}</span>  <span alpha=\"70%\">{}</span>", escapeMarkup(monitor.name), escapeMarkup(monitor.description)),
                escapeMarkup(std::format("{}×{} at {} Hz, scale {}{}{}", monitor.width, monitor.height, std::lround(monitor.refreshRate), monitor.scale,
                                         monitor.focused ? " · focused" : "", monitor.disabled ? " · disabled" : "")),
                {}, false);
            for (const auto& workspace : tree.workspaces) {
                if (workspace.monitor != monitor.name)
                    continue;
                const bool showing = workspace.id == monitor.activeWorkspace || workspace.id == monitor.specialWorkspace;
                add(std::format("      {}{}", escapeMarkup(workspaceName(workspace)), showing ? colored("  · showing", accent) : std::string{}),
                    escapeMarkup(std::format("{} {}{}", workspace.windows, workspace.windows == 1 ? "window" : "windows", workspace.hasFullscreen ? " · one fullscreen" : "")), {},
                    false);
                windowsOf(workspace);
            }
        }
        list.setRows(std::move(rows));
    }

    void LiveTreePage::embed() {
        root->removeChild(title);
        root->setMargin(0);
    }

}
