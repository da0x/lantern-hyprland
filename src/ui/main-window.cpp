// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/main-window.hpp"

#include "application/lantern-folder.hpp"
#include "application/session.hpp"
#include "ui/autostart-page.hpp"
#include "ui/bar-page.hpp"
#include "ui/default-apps-page.hpp"
#include "ui/console-page.hpp"
#include "ui/keybindings-page.hpp"
#include "ui/event-log-page.hpp"
#include "ui/live-tree-page.hpp"
#include "ui/options-page.hpp"
#include "ui/restyle.hpp"
#include "ui/screenshots-page.hpp"
#include "ui/system-info-page.hpp"
#include "ui/tabbed-page.hpp"
#include "ui/terminal-page.hpp"
#include "ui/cursors-page.hpp"
#include "ui/devices-page.hpp"
#include "ui/displays-page.hpp"
#include "ui/essentials-page.hpp"
#include "ui/idle-page.hpp"
#include "ui/input-page.hpp"
#include "ui/night-light-page.hpp"
#include "ui/accessibility-page.hpp"
#include "ui/activate-page.hpp"
#include "ui/windows-page.hpp"
#include "ui/widgets.hpp"
#include "desktop/user-themes.hpp"
#include "ui/theme-page.hpp"
#include "ui/toolkit.hpp"
#include "ui/wallpaper-page.hpp"
#include "ui/texts.hpp"
#include "ui/typing.hpp"

#include <hyprtoolkit/core/Backend.hpp>
#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/ScrollArea.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/window/Window.hpp>
#include <hyprutils/cli/Logger.hpp>

#include <algorithm>
#include <format>
#include <array>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprtoolkit::CFontSize;
        using Hyprutils::Memory::CSharedPointer;
        using Hyprutils::Memory::makeShared;

        // The sidebar grows with the text, but by less, so its longest name still fits and
        // the page keeps most of the width.
        double sidebarWidth() {
            constexpr double designed = 200;
            return designed + (scaled(designed) - designed) * 0.6;
        }

    }

    int run(const Options& options) {
        // hyprtoolkit logs every step at debug level. Only warnings and errors, unless asked.
        Hyprutils::CLI::CLogger logger;
        logger.setLogLevel(options.verbose ? Hyprutils::CLI::LOG_DEBUG : Hyprutils::CLI::LOG_WARN);
        application::moveOldFolder(foundation::currentEnvironment());
        desktop::loadUserPalettes(application::lanternFolder(foundation::currentEnvironment()) / "themes");
        Hyprtoolkit::IBackend::SBackendCreationData creation;
        creation.pLogConnection = makeShared<Hyprutils::CLI::CLoggerConnection>(logger);

        const auto backend = Hyprtoolkit::IBackend::createWithData(creation);
        if (!backend)
            return 1;

        const auto palette    = backend->getPalette();
        // Lantern's colors: hyprtoolkit's, from hyprtoolkit.conf or its defaults, until
        // the Theme page sets new ones here and every element is made again in them.
        const auto background = [palette] { return palette->m_colors.background; };
        const auto base       = [palette] { return palette->m_colors.base; };

        const auto window = Hyprtoolkit::CWindowBuilder::begin()
                                ->appTitle("Lantern for Hyprland")
                                ->appClass("lantern-hyprland")
                                // Larger with larger text, so a page still fits.
                                ->preferredSize({scaled(960), scaled(600)})
                                ->minSize({scaled(640), scaled(400)})
                                ->resizable(true)
                                ->commence();

        const auto root = Hyprtoolkit::CRectangleBuilder::begin()
                              ->color(background)
                              ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1, 1}})
                              ->commence();

        restyled(root);

        const auto body = Hyprtoolkit::CRowLayoutBuilder::begin()
                              ->gap(12)
                              ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1, 1}})
                              ->commence();
        body->setMargin(16);

        const auto sidebar = Hyprtoolkit::CColumnLayoutBuilder::begin()
                                 ->gap(4)
                                 ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_PERCENT, {sidebarWidth(), 1.0}})
                                 ->commence();
        // The pages' buttons, under the app's name, scrolled when they do not fit.
        const auto pageList = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(4)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();

        const auto content = Hyprtoolkit::CRectangleBuilder::begin()
                                 ->color(base)
                                 ->rounding(palette->m_vars.bigRounding)
                                 ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_PERCENT, {1, 1}})
                                 ->commence();
        // Growing only adds the space left over, so the base width stays minimal; sized to its
        // text instead, the panel would push past the window's right edge.
        content->setGrow(true, false);
        restyled(content);

        // The sidebar's buttons, each with its page; the open page's button is in the accent.
        std::vector<std::pair<CSharedPointer<Hyprtoolkit::CButtonElement>, CSharedPointer<Hyprtoolkit::IElement>>> pages;
        std::vector<std::pair<std::string, CSharedPointer<Hyprtoolkit::IElement>>>                                titles;
        // Told which page opens, for a page that has to tidy up when it is left.
        std::function<void(const CSharedPointer<Hyprtoolkit::IElement>&)> opening;
        // The page shown, whose button is the one in the accent.
        CSharedPointer<Hyprtoolkit::IElement> current;
        // Takes the page by value: it is called from a sidebar button's click, and rebuilding
        // that button below replaces the handler that holds the page it was given.
        const auto show = [content, &pages, &opening, &current](CSharedPointer<Hyprtoolkit::IElement> page) {
            if (opening)
                opening(page);
            content->clearChildren();
            content->addChild(page);
            // Only the two buttons that change are rebuilt: a rebuilt button keeps whatever
            // hover it had, so rebuilding the rest could leave one lit that the pointer left.
            for (const auto& [button, shown] : pages)
                if (shown == page || shown == current)
                    button->rebuild()->accent(shown == page)->commence();
            current = page;
        };

        const auto defer = [backend](std::function<void()> function) { backend->addIdle(function); };
        const application::EventLoop loop{
            .watch   = [backend](int descriptor, std::function<void()> readable) { backend->addFd(descriptor, std::move(readable)); },
            .unwatch = [backend](int descriptor) { backend->removeFd(descriptor); },
            .after =
                [backend](std::chrono::milliseconds delay, std::function<void()> callback) {
                    backend->addTimer(delay, [callback = std::move(callback)](auto, void*) { callback(); }, nullptr);
                },
        };
        KeybindingsPage keybindings{palette, defer};
        OptionsPage     optionsPage{palette, defer};
        LiveTreePage    liveTree{palette, defer};
        EventLogPage    eventLog{palette, defer};
        BarPage         bar{palette, defer, loop};
        WallpaperPage   wallpaper{palette, defer, loop};
        ThemePage       theme{palette, defer, loop};
        TerminalPage    terminal{palette, defer, loop};
        CursorsPage     cursors{palette, defer, loop};
        DisplaysPage    displays{palette, defer, loop};
        InputPage       input{palette, defer, loop, backend->systemIcons()};
        EssentialsPage  essentialsPage{palette, defer, loop};
        IdlePage        idle{palette, defer, loop};
        NightLightPage  nightLight{palette, defer, loop};
        DevicesPage     devices{palette, defer, loop};
        AutostartPage   autostart{palette, defer, loop};
        DefaultAppsPage defaultApps{palette, defer, loop};
        ScreenshotsPage screenshots{palette, defer, loop};
        AccessibilityPage accessibility{palette, defer, loop};
        WindowsPage     windows{palette, defer, loop};
        ActivatePage    activate{palette, defer};
        ConsolePage     console{palette, defer};
        // Pages that belong together, as tabs of one: the displays with the night light that
        // tints them, and the three that only look at the running Hyprland.
        SystemInfoPage  systemInfo{palette, defer, loop};
        displays.embed();
        terminal.embed();
        nightLight.embed();
        liveTree.embed();
        eventLog.embed();
        console.embed();
        TabbedPage screens{palette, defer, "Displays", {{"Screens", displays.element(), ""}, {"Night light", nightLight.element(), "Night light"}}};
        TabbedPage terminalTabs{palette, defer, "Terminal", {{"Colors", terminal.element(), ""}, {"System info", systemInfo.element(), "System info"}}};
        TabbedPage inspect{palette, defer, "Inspect",
                           {{"Tree", liveTree.element(), "Live tree"}, {"Events", eventLog.element(), "Event log"}, {"Console", console.element(), "Console"}}};
        // A wallpaper tried on the desktop and not kept goes back when its page is left; a
        // page in a tab counts as open while its tab is.
        opening = [&wallpaper, &displays, &essentialsPage, &idle, &nightLight, &devices, &autostart, &defaultApps, &screenshots, &screens,
                   &inspect, &terminalTabs, &systemInfo](const CSharedPointer<Hyprtoolkit::IElement>& opened) {
            const auto page = opened == screens.element()        ? screens.current()
                            : opened == inspect.element()        ? inspect.current()
                            : opened == terminalTabs.element()   ? terminalTabs.current()
                                                                 : opened;
            if (page == systemInfo.element())
                systemInfo.refresh();
            if (page != wallpaper.element())
                wallpaper.leave();
            if (page != nightLight.element())
                nightLight.leave();
            else
                nightLight.refresh();
            if (page == displays.element())
                displays.refresh();
            if (page == essentialsPage.element())
                essentialsPage.refresh();
            if (page == idle.element())
                idle.refresh();
            if (page == devices.element())
                devices.refresh();
            if (page == autostart.element())
                autostart.refresh();
            if (page == defaultApps.element())
                defaultApps.refresh();
            if (page == screenshots.element())
                screenshots.refresh();
        };

        const auto addPageButton = [&pageList, &show, &pages, &titles, defer](std::string_view title, CSharedPointer<Hyprtoolkit::IElement> page) {
            titles.emplace_back(std::string{title}, page);
            const auto button = Hyprtoolkit::CButtonBuilder::begin()
                                    ->label(std::string{title})
                                    ->noBorder(true)
                                    ->noBg(true)
                                    ->alignText(Hyprtoolkit::HT_FONT_ALIGN_LEFT)
                                    ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, scaled(34)}})
                                    ->onMainClick([&show, page, defer](CSharedPointer<Hyprtoolkit::CButtonElement>) { defer([&show, page] { show(page); }); })
                                    ->commence();
            restyled(button);
            pageList->addChild(button);
            pages.emplace_back(button, std::move(page));
        };

        const auto appName = Hyprtoolkit::CTextBuilder::begin()
                              ->text("<span weight=\"600\">Lantern</span>")
                              ->fontSize(CFontSize{CFontSize::HT_FONT_H2})
                              ->color([palette] { return palette->m_colors.text; })
                              ->align(Hyprtoolkit::HT_FONT_ALIGN_LEFT)
                              ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1, 1}})
                              ->commence();
        restyled(appName, CFontSize{CFontSize::HT_FONT_H2});
        // The sidebar and its buttons grow and shrink with the text, so the pages' names still fit.
        const auto resizeSidebar = [sidebar, &pages] {
            sidebar->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_PERCENT, {sidebarWidth(), 1.0}})->commence();
            for (const auto& [button, page] : pages)
                button->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, scaled(34)}})->commence();
        };
        sidebar->addChild(appName);
        sidebar->addChild(Hyprtoolkit::CNullBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {1, 12}})->commence());
        CSharedPointer<Hyprtoolkit::CScrollAreaElement> scrolled;
        if constexpr (scrollAreaWorks) {
            scrolled = Hyprtoolkit::CScrollAreaBuilder::begin()
                                      ->scrollY(true)
                                      ->scrollX(false)
                                      ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})
                                      ->commence();
            scrolled->setGrow(false, true);
            scrolled->addChild(pageList);
            sidebar->addChild(scrolled);
        } else
            sidebar->addChild(pageList);
        const auto addGroup = [&pageList, palette](const std::string& name, bool first) {
            if (!first)
                pageList->addChild(Hyprtoolkit::CNullBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {1, 8}})->commence());
            const auto line = Hyprtoolkit::CRowLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
            line->setMargin(7);
            line->addChild(markupLine(std::format("<span weight=\"600\">{}</span>", name), palette, CFontSize{CFontSize::HT_FONT_SMALL}, 0.6F));
            pageList->addChild(line);
        };
        // The look of the desktop first, since it is what Lantern is opened for most; then
        // the hardware, then how the session runs, and Hyprland's own workings last.
        addGroup("Look", true);
        addPageButton("Theme", theme.element());
        addPageButton("Wallpaper", wallpaper.element());
        addPageButton("Windows", windows.element());
        addPageButton("Bar", bar.element());
        addPageButton("Terminal", terminalTabs.element());
        addPageButton("Cursors", cursors.element());
        addGroup("Devices", false);
        addPageButton("Displays", screens.element());
        addPageButton("Keyboard and mouse", input.element());
        addPageButton("Sound and network", devices.element());
        addGroup("System", false);
        addPageButton("Essentials", essentialsPage.element());
        addPageButton("Default apps", defaultApps.element());
        addPageButton("Screenshots and clipboard", screenshots.element());
        addPageButton("Autostart", autostart.element());
        addPageButton("Idle and lock", idle.element());
        addPageButton("Accessibility", accessibility.element());
        addGroup("Hyprland", false);
        addPageButton("Keyboard shortcuts", keybindings.element());
        addPageButton("Options", optionsPage.element());
        addPageButton("Inspect", inspect.element());
        addPageButton("Activate", activate.element());
        screens.onSwitched([&opening, &screens] { opening(screens.element()); });
        inspect.onSwitched([&opening, &inspect] { opening(inspect.element()); });
        terminalTabs.onSwitched([&opening, &terminalTabs] { opening(terminalTabs.element()); });
        // The page asked for by --page: one of the sidebar's, or a tab of one.
        CSharedPointer<Hyprtoolkit::IElement> firstPage = theme.element();
        if (const auto first = std::ranges::find(titles, options.page, &std::pair<std::string, CSharedPointer<Hyprtoolkit::IElement>>::first); first != titles.end())
            firstPage = first->second;
        else if (screens.select(options.page))
            firstPage = screens.element();
        else if (inspect.select(options.page))
            firstPage = inspect.element();
        else if (terminalTabs.select(options.page))
            firstPage = terminalTabs.element();
        show(firstPage);
        // Lantern opened on a page further down has its button in view, in the middle.
        if (scrolled && firstPage != theme.element()) {
            const auto opened = std::ranges::find(pages, firstPage, &std::pair<CSharedPointer<Hyprtoolkit::CButtonElement>, CSharedPointer<Hyprtoolkit::IElement>>::second);
            auto       pending = std::make_shared<bool>(true);
            scrolled->setRepositioned([weak = Hyprutils::Memory::CWeakPointer<Hyprtoolkit::CScrollAreaElement>{scrolled}, button = opened->first, defer, pending] {
                if (!*pending)
                    return;
                *pending = false;
                defer([weak, button] {
                    if (const auto area = weak.lock())
                        area->setScroll({0.0, std::max(0.0, button->posFromParent().y - area->size().y / 2)});
                });
            });
        }

        body->addChild(sidebar);
        body->addChild(content);
        root->addChild(body);
        window->m_rootElement = root;

        std::optional<application::Session> session;
        session.emplace(
            loop,
            application::Listeners{
                .connection =
                    [&keybindings, &optionsPage, &liveTree, &eventLog](application::Connection connection, const std::string& reason) {
                        keybindings.showConnection(connection, reason);
                        optionsPage.showConnection(connection, reason);
                        liveTree.showConnection(connection, reason);
                        eventLog.showConnection(connection, reason);
                    },
                .keybindings = [&keybindings](const auto& result) { keybindings.showKeybindings(result); },
                .options     = [&optionsPage](const auto& result) { optionsPage.showOptions(result); },
                .tree =
                    [&liveTree, &eventLog](const auto& result) {
                        liveTree.showTree(result);
                        if (result)
                            eventLog.learnWindows(*result);
                    },
                .events      = [&eventLog](const auto& events) { eventLog.addEvents(events); },
            });

        console.onAsk([&session](std::string typed, auto answered) { session->ask(std::move(typed), std::move(answered)); });

        // The session and the setup pages go first: they unwatch descriptors, which needs a live event loop.
        const auto quit = [&session, &bar, &wallpaper, &theme, &windows, window, backend] {
            session.reset();
            bar.close();
            wallpaper.leave(true);
            wallpaper.close();
            theme.close();
            windows.close();
            window->close();
            backend->destroy();
        };
        bar.setWindow(window);
        window->m_events.closeRequest.listenStatic(quit);
        // Q quits too, unless it was typed into a text box: that box takes the key after the
        // window, so whether one did shows in its text once the key is handled.
        window->m_events.keyboardKey.listenStatic([quit, defer](const Hyprtoolkit::Input::SKeyboardKeyEvent& key) {
            constexpr std::uint32_t held = Hyprtoolkit::Input::HT_MODIFIER_CTRL | Hyprtoolkit::Input::HT_MODIFIER_ALT | Hyprtoolkit::Input::HT_MODIFIER_META;
            if (!key.down || key.repeat || key.utf8 != "q" || (key.modMask & held) != 0)
                return;
            defer([quit, before = textboxTexts()] {
                if (textboxTexts() == before)
                    quit();
            });
        });
        // Every element made in the old colors is made again: the long-lived ones in place,
        // and the pages' contents, some with colors baked into their text, from their data.
        const auto redraw = [&keybindings, &optionsPage, &liveTree, &eventLog, &bar, &wallpaper, &theme, &terminal, &systemInfo, &cursors, &displays, &input, &essentialsPage, &idle, &nightLight, &devices, &autostart, &defaultApps, &screenshots, &accessibility, &windows, &activate, &console](bool resized) {
            // The pages first, so the elements they let go of are not drawn again in between;
            // what they make is already right, so only what was there before is rebuilt.
            // The Theme page draws itself again after its own change; a new text size is another's.
            const auto before = restyleCount();
            if (resized)
                theme.restyle();
            keybindings.restyle();
            optionsPage.restyle();
            liveTree.restyle();
            eventLog.restyle();
            terminal.restyle();
            systemInfo.restyle();
            cursors.restyle();
            displays.restyle();
            input.restyle();
            essentialsPage.restyle();
            idle.restyle();
            nightLight.restyle();
            devices.restyle();
            autostart.restyle();
            defaultApps.restyle();
            screenshots.restyle();
            bar.restyle();
            wallpaper.restyle();
            accessibility.restyle();
            windows.restyle();
            activate.restyle();
            console.restyle();
            restyleAll(resized, before);
        };
        theme.onRecolored([redraw] { redraw(false); });
        // The terminal's themes follow the contrast and color vision; Lantern's text, the size.
        accessibility.onApplied([&terminal, palette, redraw, defer, window, loop, resizeSidebar](const desktop::Accessibility& now) {
            terminal.adjust(now.contrast, now.vision);
            auto&      sizes  = palette->m_vars;
            const auto scaled = desktop::toolkitTextSizes(now.textScale);
            const auto size   = [&scaled](std::string_view key) { return std::stoi(std::ranges::find(scaled, key, &std::pair<std::string, std::string>::first)->second); };
            const std::array current{sizes.h1Size, sizes.h2Size, sizes.h3Size, sizes.fontSize, sizes.smallFontSize};
            sizes.h1Size        = size("h1_size");
            sizes.h2Size        = size("h2_size");
            sizes.h3Size        = size("h3_size");
            sizes.fontSize      = size("font_size");
            sizes.smallFontSize = size("small_font_size");
            if (current == std::array{sizes.h1Size, sizes.h2Size, sizes.h3Size, sizes.fontSize, sizes.smallFontSize})
                return;
            // The window grows with the text, so the page still fits: a page laid out taller
            // than its window comes out wrong. It grows first, with the sidebar, whose new
            // width is laid out with the window's new size, and the text follows once the
            // compositor has had time to apply it; the compositor has the last word. With
            // smaller text the window keeps its size.
            if (sizes.fontSize > current[3]) {
                resizeSidebar();
                if (const auto scale = window->scale(); scale > 0)
                    window->setSize(window->pixelSize() / scale * (static_cast<double>(sizes.fontSize) / current[3]));
                loop.after(std::chrono::milliseconds{200}, [redraw] { redraw(true); });
            } else {
                defer([redraw, resizeSidebar] {
                    redraw(true);
                    resizeSidebar();
                });
            }
        });

        window->open();
        session->start();
        backend->enterLoop();
        return 0;
    }

}
