// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/wallpaper-page.hpp"

#include "application/command.hpp"
#include "application/config-check.hpp"
#include "application/lantern-folder.hpp"
#include "desktop/system.hpp"
#include "configuration/main-config.hpp"
#include "desktop/clipboard.hpp"
#include "desktop/trash.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"
#include "ui/toolkit.hpp"
#include "ui/widgets.hpp"

#include <hyprtoolkit/element/Combobox.hpp>
#include <hyprtoolkit/element/Image.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/ScrollArea.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <sstream>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprtoolkit::CFontSize;
        using Hyprutils::Memory::CSharedPointer;

        constexpr std::size_t perRow       = 4;
        constexpr double      cardShare    = 0.24;
        constexpr double      thumbHeight  = 110;
        // A card: the image and the margin around it. The chosen image's name is beside the
        // preview.
        double cardHeight() {
            return thumbHeight + 2 * 8;
        }
        constexpr double      previewWidth = 400;
        // A 16:9 screen.
        constexpr double      previewHeight = previewWidth * 9 / 16;
        // The preview's row, with room for the column beside it.
        constexpr double      chooserHeight = previewHeight;
        constexpr auto        changeName    = "Wallpaper";

        Hyprtoolkit::eImageFitMode imageFit(desktop::FitMode fit) {
            switch (fit) {
                case desktop::FitMode::cover: return Hyprtoolkit::IMAGE_FIT_MODE_COVER;
                case desktop::FitMode::contain: return Hyprtoolkit::IMAGE_FIT_MODE_CONTAIN;
                case desktop::FitMode::fill: return Hyprtoolkit::IMAGE_FIT_MODE_STRETCH;
                case desktop::FitMode::tile: return Hyprtoolkit::IMAGE_FIT_MODE_TILE;
            }
            return Hyprtoolkit::IMAGE_FIT_MODE_COVER;
        }

        std::string fitName(desktop::FitMode fit) {
            for (const auto& info : desktop::fitModes())
                if (info.mode == fit)
                    return std::string{info.name};
            return {};
        }

        desktop::FitMode fitFromWord(std::string_view word) {
            for (const auto& info : desktop::fitModes())
                if (info.word == word)
                    return info.mode;
            return desktop::FitMode::cover;
        }

        std::string_view wordOf(desktop::FitMode fit) {
            for (const auto& info : desktop::fitModes())
                if (info.mode == fit)
                    return info.word;
            return "cover";
        }

        const std::array systemFolders{std::filesystem::path{"/usr/share/hypr"}, std::filesystem::path{"/usr/share/backgrounds"}};

    }

    WallpaperPage::WallpaperPage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop) :
        palette{std::move(palette)},
        defer{std::move(defer)},
        loop{std::move(loop)},
        diff{this->palette, this->defer,
             {RowList::Column{.width = 24, .monospace = true, .secondary = false, .markup = true, .heading = ""},
              RowList::Column{.width = 0, .monospace = true, .secondary = false, .markup = true, .heading = ""}},
             20},
        hyprpaper{this->palette, this->defer, this->loop, "hyprpaper", "hyprpaper", [this] {
                      if (!reviewing)
                          showOverview();
                  }},
        awww{this->palette, this->defer, this->loop, "awww-daemon", "awww",
             [this] {
                 // Apply started the daemon: now it can be given the image.
                 if (awwwAfterStart && awww.running() && awwwCommand) {
                     if (const auto arguments = desktop::awwwArguments(*awwwAfterStart))
                         (void)application::runCommand(this->loop, *awwwCommand, *arguments, [this](bool, std::string) { askAwww(); });
                     awwwAfterStart.reset();
                 }
                 askAwww();
                 if (!reviewing)
                     showOverview();
             }},
        files{this->loop} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        root->addChild(pageTitle("Wallpaper", this->palette));
        root->addChild(paragraph("Lantern draws no wallpaper of its own. It uses hyprpaper, Hyprland's own wallpaper program, or awww, which fades between "
                                 "images, and shows the images it finds for you to choose from. It never installs anything: when something is missing, it "
                                 "shows the command for you to run.",
                                 this->palette));
        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        switchArea = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        root->addChild(switchArea);
        root->addChild(body);
        footer = leftRow();
        root->addChild(footer);

        const auto environment = foundation::currentEnvironment();
        settings               = application::loadSettings(application::lanternFolder(environment));
        // Until the user chooses: awww if only it is installed, otherwise hyprpaper.
        if (settings.wallpaperProgram == "awww" ||
            (settings.wallpaperProgram.empty() && desktop::wallpaperRequirement(desktop::WallpaperProgram::awww, environment).met() &&
             !desktop::wallpaperRequirement(desktop::WallpaperProgram::hyprpaper, environment).met()))
            program = desktop::WallpaperProgram::awww;
        showSwitch();
        check();
    }

    void WallpaperPage::close() {
        hyprpaper.close();
        awww.close();
    }

    ProgramSwitch& WallpaperPage::current() {
        return program == desktop::WallpaperProgram::awww ? awww : hyprpaper;
    }

    void WallpaperPage::showSwitch() {
        switchArea->clearChildren();
        switchArea->addChild(current().element());
    }

    std::optional<desktop::Wallpaper> WallpaperPage::kept() const {
        if (program == desktop::WallpaperProgram::awww)
            return awwwShows;
        return desktop::currentWallpaper(foundation::currentEnvironment());
    }

    void WallpaperPage::choose(desktop::WallpaperProgram chosen) {
        if (chosen == program)
            return;
        leave();
        program                   = chosen;
        settings.wallpaperProgram = chosen == desktop::WallpaperProgram::awww ? "awww" : "hyprpaper";
        if (const auto saved = application::saveSettings(settings, application::lanternFolder(foundation::currentEnvironment())); !saved)
            outcome = std::format("Lantern could not save its settings: {}.", saved.error());
        if (const auto set = kept())
            selected = set;
        if (chosen == desktop::WallpaperProgram::awww && selected && selected->fit == desktop::FitMode::tile)
            selected->fit = desktop::FitMode::cover;
        showSwitch();
        showOverview();
    }

    void WallpaperPage::askAwww() {
        if (!awww.running() || !awwwCommand)
            return;
        (void)application::runCommand(loop, *awwwCommand, {"query", "-j"}, [this](bool succeeded, std::string output) {
            const auto image = succeeded ? desktop::awwwImage(output) : std::nullopt;
            awwwShows        = image ? std::optional{desktop::Wallpaper{*image, fitFromWord(settings.awwwFit)}} : std::nullopt;
            if (program == desktop::WallpaperProgram::awww && !tried && awwwShows)
                selected = awwwShows;
            if (!reviewing)
                showOverview();
        });
    }

    void WallpaperPage::leave(bool closing) {
        if (!tried)
            return;
        tried = false;
        if (program == desktop::WallpaperProgram::awww) {
            // awww remembers every image it is given, so the one it kept is given again.
            if (awwwShows && awwwCommand)
                if (const auto arguments = desktop::awwwArguments(*awwwShows))
                    (void)application::runCommand(loop, *awwwCommand, *arguments, [](bool, std::string) {});
            selected = awwwShows ? awwwShows : selected;
            return;
        }
        const auto set = desktop::currentWallpaper(foundation::currentEnvironment());
        selected       = set ? set : selected;
        // With nothing in hyprpaper.conf there is nothing to show instead, so hyprpaper
        // starts again empty.
        const auto arguments = set ? desktop::showWallpaperArguments(*set) : std::unexpected<std::string>{"nothing set"};
        if ((!arguments || !hyprctl || !application::runCommand(loop, *hyprctl, *arguments, [](bool, std::string) {})) && !closing)
            hyprpaper.restart();
    }

    void WallpaperPage::tryOnDesktop() {
        if (program == desktop::WallpaperProgram::awww) {
            if (!awww.running() || !awwwCommand || !selected)
                return;
            const auto arguments = desktop::awwwArguments(*selected);
            if (!arguments) {
                outcome = arguments.error();
                return;
            }
            tried              = selected != awwwShows;
            const auto started = application::runCommand(loop, *awwwCommand, *arguments, [this](bool succeeded, std::string output) {
                if (succeeded)
                    return;
                outcome = std::format("awww could not show it: {}", output.empty() ? std::string{"awww failed"} : output);
                if (!reviewing)
                    showOverview();
            });
            if (!started)
                outcome = started.error();
            return;
        }
        if (!hyprpaper.running() || !hyprctl || !selected)
            return;
        const auto arguments = desktop::showWallpaperArguments(*selected);
        if (!arguments) {
            outcome = arguments.error();
            return;
        }
        tried = selected != desktop::currentWallpaper(foundation::currentEnvironment());
        const auto started = application::runCommand(loop, *hyprctl, *arguments, [this](bool succeeded, std::string output) {
            if (succeeded)
                return;
            outcome = std::format("hyprpaper could not show it: {}", output.empty() ? std::string{"hyprctl failed"} : output);
            if (!reviewing)
                showOverview();
        });
        if (!started)
            outcome = started.error();
    }

    void WallpaperPage::restyle() {
        if (!reviewing)
            showOverview();
    }

    void WallpaperPage::check() {
        const auto environment = foundation::currentEnvironment();
        hyprctl                = desktop::findProgram("hyprctl", foundation::variable(environment, "PATH").value_or(""));
        awwwCommand            = desktop::findProgram("awww", foundation::variable(environment, "PATH").value_or(""));
        hyprpaper.refresh();
        awww.refresh();
        askAwww();
        wallpapers = desktop::findWallpapers(environment, systemFolders);
        const auto current = kept();
        if (current && !std::ranges::contains(wallpapers, current->image))
            wallpapers.insert(wallpapers.begin(), current->image);
        if (!selected)
            selected = current ? current : wallpapers.empty() ? std::nullopt : std::optional{desktop::Wallpaper{wallpapers.front(), desktop::FitMode::cover}};
        pageStart = std::min(pageStart, wallpapers.empty() ? 0 : wallpapers.size() - 1);
        if (!reviewing)
            showOverview();
    }

    CSharedPointer<Hyprtoolkit::IElement> WallpaperPage::chooser() {
        const auto environment = foundation::currentEnvironment();
        const auto whole       = leftRow(chooserHeight);
        whole->rebuild()->gap(20)->commence();

        // The chosen image as the screen would show it.
        const auto frame = Hyprtoolkit::CRectangleBuilder::begin()
                               ->color([] { return Hyprtoolkit::CHyprColor{0, 0, 0, 1}; })
                               ->rounding(palette->m_vars.smallRounding)
                               ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {previewWidth, previewHeight}})
                               ->commence();
        if (!preview) {
            previewImage = selected->image;
            preview      = Hyprtoolkit::CImageBuilder::begin()
                          ->path(selected->image.string())
                          ->fitMode(imageFit(selected->fit))
                          ->rounding(palette->m_vars.smallRounding)
                          ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})
                          ->commence();
        }
        frame->addChild(preview);
        whole->addChild(frame);

        // A fixed share of the row and its full height: a column that starts narrow and
        // grows, or takes the height of its content, measures its paragraphs a letter to a
        // line and comes out far too tall.
        const auto side =
            Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(5)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {0.5, chooserHeight}})->commence();
        nameText   = markupLine(" ", palette);
        folderText = paragraph(" ", palette, true);
        side->addChild(nameText);
        side->addChild(folderText);
        side->addChild(markupLine("<span weight=\"600\">Fit</span>", palette));
        const auto fits = leftRow();
        fits->rebuild()->gap(4)->commence();
        fitButtons.clear();
        for (const auto& info : desktop::fitModes()) {
            if (info.mode == desktop::FitMode::tile && program == desktop::WallpaperProgram::awww)
                continue;
            const auto made = button(std::string{info.name}, 84, false, [this, mode = info.mode] {
                defer([this, mode] {
                    selected->fit = mode;
                    tryOnDesktop();
                    updateSelection();
                });
            });
            fitButtons.emplace_back(info.mode, made);
            fits->addChild(made);
        }
        side->addChild(fits);
        const auto remove = leftRow();
        remove->addChild(button("Move to trash", 150, false, [this] { defer([this] { trashSelected(); }); }));
        side->addChild(remove);
        status = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(5)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        statusShown.clear();
        side->addChild(status);
        whole->addChild(side);
        return whole;
    }

    void WallpaperPage::trashSelected() {
        if (!selected)
            return;
        const auto environment = foundation::currentEnvironment();
        const auto image       = selected->image;
        const auto home        = std::filesystem::path{foundation::variable(environment, "HOME").value_or("/nonexistent")};
        const auto inside      = image.lexically_relative(home);
        const auto set         = kept();
        if (inside.empty() || inside.native().starts_with(".."))
            outcome = "Only your own pictures can go to the trash; this one came with the system.";
        else if (set && set->image == image)
            outcome = "It is your wallpaper now. Keep another one first, and then it can go to the trash.";
        else if (const auto moved = desktop::moveToTrash(image, desktop::trashFolder(environment)); !moved)
            outcome = std::format("{} was not moved to the trash: {}.", image.filename().string(), moved.error());
        else {
            outcome = std::format("{} is in the trash, where your file manager can put it back.", image.filename().string());
            std::erase(wallpapers, image);
            // The desktop goes back to the kept wallpaper if it was trying this one.
            if (tried)
                leave(false);
            selected = set ? set : wallpapers.empty() ? std::nullopt : std::optional{desktop::Wallpaper{wallpapers.front(), desktop::FitMode::cover}};
        }
        showOverview();
    }

    void WallpaperPage::updateSelection() {
        if (!selected || !preview)
            return;
        if (previewImage != selected->image) {
            previewImage = selected->image;
            preview->rebuild()->path(selected->image.string())->fitMode(imageFit(selected->fit))->commence();
        } else {
            preview->rebuild()->fitMode(imageFit(selected->fit))->commence();
        }
        nameText->rebuild()->text(std::format("<span weight=\"600\">{}</span>", escapeMarkup(selected->image.filename().string())))->commence();
        folderText->rebuild()->text(escapeMarkup(shortPath(selected->image.parent_path())))->commence();
        for (const auto& [mode, made] : fitButtons)
            made->rebuild()->accent(mode == selected->fit)->commence();
        for (const auto& [image, card] : cards) {
            const bool chosen = image == selected->image;
            card->rebuild()->borderColor([palette = palette, chosen] { return chosen ? palette->m_colors.accent : Hyprtoolkit::CHyprColor{0, 0, 0, 0}; })->commence();
        }
        fillStatus();
    }

    void WallpaperPage::fillStatus() {
        const auto environment = foundation::currentEnvironment();
        const bool awwwChosen  = program == desktop::WallpaperProgram::awww;
        const auto config      = configuration::mainConfig(configuration::lanternLaunch());
        const auto planned     = config ? desktop::planWallpaperSetup(program, *selected, *config, environment)
                                        : std::expected<std::vector<configuration::FileChange>, std::string>{std::unexpect, config.error()};
        const auto& other      = awwwChosen ? hyprpaper : awww;
        const bool shows       = awwwChosen ? awww.running() && selected == awwwShows : hyprpaper.running();
        const bool applied     = planned && planned->empty() && !other.running() && shows && !tried;
        const bool undoable    = configuration::latestChange(application::backupsFolder(environment), changeName).has_value();
        const bool off         = desktop::wallpaperRequirement(program, environment).met() && !current().running();
        const auto shown = std::format("{}|{}|{}|{}|{}|{}|{}", awwwChosen, tried, applied, planned ? "" : planned.error(), undoable, off, outcome);
        if (shown == statusShown)
            return;
        statusShown = shown;
        status->clearChildren();

        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> buttons;
        std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>> away;
        if (undoable)
            away.push_back(button("Undo last change", 160, false, [this] { defer([this] { undo(); }); }));
        if (!planned)
            status->addChild(paragraph(std::format("Lantern cannot set it up: {}.", planned.error()), palette));
        else if (applied)
            status->addChild(markupLine(std::format("{}  Applied: {} shows it, and starts when you log in.", colored("✓", hex(palette->m_colors.accent)),
                                                    awwwChosen ? "awww" : "hyprpaper"),
                                        palette));
        else
            buttons.push_back(button("Review changes", 160, true, [this] { defer([this] { showReview(); }); }));
        setFooter(footer, buttons, nullptr, away);

        if (tried)
            status->addChild(paragraph("On your desktop now, to try. It goes back when you leave this page, unless you apply it.", palette, true));
        if (off) {
            const auto name = awwwChosen ? std::string{"awww"} : std::string{"hyprpaper"};
            status->addChild(paragraph(std::format("{} is off. Turn it on to try wallpapers on the desktop as you pick them.", name), palette, true));
            const auto on = leftRow();
            on->addChild(button(std::format("Turn {} on", name), 190, false, [this] { defer([this] { current().turnOn(); }); }));
            status->addChild(on);
        }
        if (!outcome.empty())
            status->addChild(paragraph(outcome, palette));
        if (!awwwChosen) {
            if (const auto text = desktop::planWallpaper(*selected, environment); text && text->current) {
                if (const auto own = desktop::monitorsWithOwnWallpaper(*text->current); !own.empty()) {
                    std::string names;
                    for (const auto& monitor : own)
                        names += (names.empty() ? "" : ", ") + monitor;
                    status->addChild(paragraph(std::format("hyprpaper.conf gives {} a wallpaper of its own, which this does not change.", names), palette, true));
                }
            }
        }
    }

    CSharedPointer<Hyprtoolkit::IElement> WallpaperPage::thumbnail(const std::filesystem::path& image) {
        const bool chosen = selected && selected->image == image;
        const auto card   = Hyprtoolkit::CRectangleBuilder::begin()
                              ->color([palette = palette] { return palette->m_colors.alternateBase; })
                              ->rounding(palette->m_vars.bigRounding)
                              ->borderColor([palette = palette, chosen] { return chosen ? palette->m_colors.accent : Hyprtoolkit::CHyprColor{0, 0, 0, 0}; })
                              ->borderThickness(2)
                              ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {cardShare, 1.0}})
                              ->commence();
        const auto inside = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        inside->setMargin(8);
        auto& picture = thumbnails[image];
        if (!picture)
            picture = Hyprtoolkit::CImageBuilder::begin()
                          ->path(image.string())
                          ->fitMode(Hyprtoolkit::IMAGE_FIT_MODE_COVER)
                          ->rounding(palette->m_vars.smallRounding)
                          ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, thumbHeight}})
                          ->commence();
        inside->addChild(picture);
        card->addChild(inside);
        cards[image] = card;
        card->setReceivesMouse(true);
        // Paged by the wheel only where the scroll area cannot be used; it scrolls by itself.
        if constexpr (!scrollAreaWorks)
            card->setMouseAxis([this](Hyprtoolkit::Input::eAxisAxis axis, float delta) { scroll(axis, delta); });
        card->setMouseButton([this, image](Hyprtoolkit::Input::eMouseButton button, bool pressed) {
            if (button != Hyprtoolkit::Input::MOUSE_BUTTON_LEFT || !pressed)
                return;
            defer([this, image] {
                selected = desktop::Wallpaper{image, selected ? selected->fit : desktop::FitMode::cover};
                outcome.clear();
                tryOnDesktop();
                updateSelection();
            });
        });
        return card;
    }

    CSharedPointer<Hyprtoolkit::IElement> WallpaperPage::grid() {
        const auto section = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        // A scroll area takes all the height the page leaves; the paged row keeps its own.
        if constexpr (scrollAreaWorks) {
            section->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
            section->setGrow(false, true);
        }
        const auto top     = leftRow();
        gridTitle          = markupLine(" ", palette);
        // Sized to its words: a text that starts narrow and grows would wrap at every letter
        // first, and the row would take that height.
        gridTitle->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->noEllipsize(false)->commence();
        top->addChild(gridTitle);
        const auto rest = space(1);
        rest->setGrow(true, false);
        top->addChild(rest);
        top->addChild(button("Add from files…", 160, false, [this] { defer([this] { addFromFiles(); }); }));
        section->addChild(top);
        // A row of images at a time; the mouse wheel moves to the next or the previous.
        gridRows = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(12)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        if constexpr (scrollAreaWorks) {
            gridRows->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
            gridRows->setGrow(false, true);
        }
        section->addChild(gridRows);
        fillGrid();
        return section;
    }

    void WallpaperPage::fillGrid() {
        cards.clear();
        gridRows->clearChildren();
        // Every image in a scroll area, or a row of them paged in; see ui/toolkit.hpp.
        const auto first = scrollAreaWorks ? 0 : pageStart;
        const auto last  = scrollAreaWorks ? wallpapers.size() : std::min(wallpapers.size(), pageStart + pageSize);
        const auto rows  = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(12)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        for (std::size_t start = first; start < last; start += perRow) {
            const auto line = leftRow(0);
            // A fixed height: sized to the cards, the row came out taller than they are.
            line->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, cardHeight()}})->gap(12)->commence();
            for (std::size_t index = start; index < std::min(last, start + perRow); ++index)
                line->addChild(thumbnail(wallpapers[index]));
            rows->addChild(line);
        }
        if constexpr (scrollAreaWorks) {
            const auto area = Hyprtoolkit::CScrollAreaBuilder::begin()
                                  ->scrollY(true)
                                  ->scrollX(false)
                                  ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})
                                  ->commence();
            area->addChild(rows);
            if (const auto at = std::ranges::find(wallpapers, selected ? selected->image : std::filesystem::path{}); at != wallpapers.end())
                scrollWhenLaidOut(area, Hyprutils::Math::Vector2D{0.0, static_cast<double>(static_cast<std::size_t>(at - wallpapers.begin()) / perRow) * (cardHeight() + 12)}, defer);
            gridRows->addChild(area);
            gridTitle->rebuild()
                ->text(std::format("<span weight=\"600\">Wallpapers</span>   {}{}", wallpapers.size(), wallpapers.size() > perRow ? " · scroll for more" : ""))
                ->commence();
            return;
        }
        gridRows->addChild(rows);
        const auto end  = std::min(wallpapers.size(), pageStart + pageSize);
        const auto more = wallpapers.size() > pageSize ? " · scroll for more" : "";
        gridTitle->rebuild()->text(std::format("<span weight=\"600\">Wallpapers</span>   {}–{} of {}{}", pageStart + 1, end, wallpapers.size(), more))->commence();
    }

    void WallpaperPage::scroll(Hyprtoolkit::Input::eAxisAxis axis, float delta) {
        if (axis != Hyprtoolkit::Input::AXIS_AXIS_VERTICAL || delta == 0.F)
            return;
        const auto last = wallpapers.size() > pageSize ? (wallpapers.size() - pageSize + perRow - 1) / perRow * perRow : 0;
        const auto next = delta > 0 ? std::min(last, pageStart + perRow) : pageStart - std::min(pageStart, perRow);
        if (next == pageStart)
            return;
        pageStart = next;
        // Not from inside the wheel's handler: the card it came through is replaced.
        defer([this] {
            fillGrid();
            updateSelection();
        });
    }

    void WallpaperPage::showOverview() {
        reviewing = false;
        changes.clear();
        body->clearChildren();

        const auto environment  = foundation::currentEnvironment();
        const auto distribution = desktop::currentDistribution();
        const auto copier       = desktop::clipboardProgram(foundation::variable(environment, "PATH").value_or(""));
        const bool awwwChosen   = program == desktop::WallpaperProgram::awww;
        const auto requirement  = desktop::wallpaperRequirement(program, environment);
        const bool allMet       = requirement.met();

        // Which program draws the wallpaper.
        const auto programs = leftRow();
        const auto label    = markupLine("<span weight=\"600\">Program</span>", palette);
        label->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {90.0, 1.0}})->commence();
        programs->addChild(label);
        programs->addChild(Hyprtoolkit::CComboboxBuilder::begin()
                               ->items({"hyprpaper", "awww"})
                               ->currentItem(awwwChosen ? 1 : 0)
                               ->onChanged([this](CSharedPointer<Hyprtoolkit::CComboboxElement>, std::size_t index) {
                                   defer([this, index] { choose(index == 1 ? desktop::WallpaperProgram::awww : desktop::WallpaperProgram::hyprpaper); });
                               })
                               ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {scaled(180.0), scaled(34.0)}})
                               ->commence());
        body->addChild(programs);

        // Two wallpaper programs at once draw over each other.
        auto& other = awwwChosen ? hyprpaper : awww;
        if (other.running()) {
            const auto otherName = awwwChosen ? std::string{"hyprpaper"} : std::string{"awww"};
            const auto warning   = leftRow();
            const auto text      = markupLine(std::format("{} is running too, and the two draw over each other.", otherName), palette);
            text->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
            warning->addChild(text);
            warning->addChild(button(std::format("Turn {} off", otherName), 180, false, [this, &other] { defer([&other] { other.turnOff(); }); }));
            body->addChild(warning);
        }

        const bool checklist = !allMet || !settings.wallpaperRequirementsDismissed;
        // One row: with the footer, a second would not fit, and hyprtoolkit 0.6.0 lays out a
        // column taller than the window badly rather than cutting it off.
        pageSize             = perRow;
        pageStart            = pageStart / pageSize * pageSize;
        // One line each, so the images still fit below: hyprtoolkit 0.6.0 lays out a column
        // taller than the window badly rather than cutting it off.
        if (checklist) {
            const auto bad    = darkTheme(*palette) ? std::string{"#f38ba8"} : std::string{"#c62828"};
            const auto good   = darkTheme(*palette) ? std::string{"#a6e3a1"} : std::string{"#2f7d32"};
            const auto install = desktop::installCommand(requirement, distribution);
            if (!allMet && !install.empty()) {
                body->addChild(commandRow(install, copier, palette, defer,
                                          std::format("{}  <span weight=\"600\">{}</span> is not installed. Run this, then press Check again:", colored("✗", bad),
                                                      escapeMarkup(requirement.name))));
            } else if (!allMet) {
                body->addChild(requirementBlock(requirement, distribution, copier, palette, defer, [this] { check(); }));
            }
            const auto actions = leftRow();
            if (allMet) {
                const auto found = markupLine(std::format("{}  <span weight=\"600\">{}</span>   {}", colored("✓", good), escapeMarkup(requirement.name),
                                                          escapeMarkup(requirement.found.value_or(""))),
                                              palette);
                found->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
                actions->addChild(found);
            }
            if (!allMet && !install.empty())
                actions->addChild(button("Check again", 140, false, [this] { defer([this] { check(); }); }));
            if (allMet)
                actions->addChild(button("Dismiss", 110, false, [this] {
                    defer([this] {
                        settings.wallpaperRequirementsDismissed = true;
                        if (const auto saved = application::saveSettings(settings, application::lanternFolder(foundation::currentEnvironment())); !saved)
                            outcome = std::format("Lantern could not save its settings: {}.", saved.error());
                        showOverview();
                    });
                }));
            body->addChild(actions);
        }

        if (!selected) {
            body->addChild(paragraph("No images were found in ~/Pictures/Wallpapers, ~/Pictures, ~/.local/share/backgrounds, /usr/share/hypr or /usr/share/backgrounds. "
                                     "Put some in ~/Pictures/Wallpapers and press Check again.",
                                     palette));
            setFooter(footer, {});
            return;
        }
        body->addChild(chooser());
        body->addChild(space(6));
        body->addChild(grid());
        updateSelection();
    }

    void WallpaperPage::showReview() {
        const auto environment = foundation::currentEnvironment();
        const auto config      = configuration::mainConfig(configuration::lanternLaunch());
        auto       planned     = config ? desktop::planWallpaperSetup(program, *selected, *config, environment)
                                        : std::expected<std::vector<configuration::FileChange>, std::string>{std::unexpect, config.error()};
        if (!planned) {
            outcome = planned.error();
            return showOverview();
        }
        reviewing = true;
        changes   = std::move(*planned);
        outcome.clear();
        body->clearChildren();

        const bool  awwwChosen = program == desktop::WallpaperProgram::awww;
        const auto  name       = awwwChosen ? std::string{"awww"} : std::string{"hyprpaper"};
        const auto& other      = awwwChosen ? hyprpaper : awww;
        std::vector<std::string> steps;
        if (!changes.empty())
            steps.emplace_back("Write the changes below. A copy of each file is kept first, so it can be undone.");
        if (other.running())
            steps.push_back(std::format("Turn {} off, so that only {} draws the wallpaper.", awwwChosen ? "hyprpaper" : "awww", name));
        if (!current().running())
            steps.push_back(std::format("Start {}.", name));
        else if (!awwwChosen && !changes.empty())
            steps.emplace_back("Restart hyprpaper, so it reads hyprpaper.conf.");
        if (awwwChosen)
            steps.emplace_back("Give awww the image. awww shows it again whenever it starts.");

        body->addChild(space(4));
        body->addChild(heading(std::format("Review: {}, {}, with {}", selected->image.filename().string(), fitName(selected->fit), name), palette));
        body->addChild(paragraph("Applying will:", palette));
        for (const auto& step : steps)
            body->addChild(paragraph(std::format("•  {}", step), palette, true));
        if (changes.empty())
            body->addChild(paragraph("No files change.", palette, true));
        setFooter(footer, {button("Cancel", 110, false, [this] { defer([this] { showOverview(); }); }), button("Apply", 110, true, [this] { defer([this] { apply(); }); })});
        if (!changes.empty()) {
            diff.setRows(diffRows(changes, *palette));
            body->addChild(diff.element());
        }
    }

    void WallpaperPage::apply() {
        if (!reviewing)
            return;
        const bool  awwwChosen = program == desktop::WallpaperProgram::awww;
        auto&       other      = awwwChosen ? hyprpaper : awww;
        std::optional<std::filesystem::path> record;
        if (!changes.empty()) {
            auto written = configuration::applyChanges(changes, application::backupsFolder(foundation::currentEnvironment()),
                                                       std::format("{}: {}, {}, with {}", changeName, selected->image.string(), fitName(selected->fit),
                                                                   awwwChosen ? "awww" : "hyprpaper"));
            if (!written) {
                outcome = std::format("Nothing was changed: {}.", written.error());
                return showOverview();
            }
            record = *written;
        }
        tried = false;
        other.turnOff();

        if (awwwChosen) {
            settings.awwwFit = std::string{wordOf(selected->fit)};
            (void)application::saveSettings(settings, application::lanternFolder(foundation::currentEnvironment()));
            if (awww.running() && awwwCommand) {
                if (const auto arguments = desktop::awwwArguments(*selected))
                    (void)application::runCommand(loop, *awwwCommand, *arguments, [this](bool, std::string) { askAwww(); });
            } else {
                awwwAfterStart = selected;
                awww.turnOn();
            }
            outcome = "Applied. awww shows it, and starts when you log in.";
        } else if (!hyprpaper.running()) {
            hyprpaper.turnOn();
            outcome = "Applied. hyprpaper is starting with it, and starts when you log in.";
        } else if (record) {
            outcome = "Applied. hyprpaper is restarting…";
            hyprpaper.restartToRead(*record, [this](std::string what) {
                outcome = std::move(what);
                if (!reviewing)
                    showOverview();
            });
        }
        if (record)
            checkConfig(*record);
        showOverview();
    }

    void WallpaperPage::checkConfig(const std::filesystem::path& record) {
        if (!hyprctl)
            return;
        application::checkHyprlandConfig(loop, *hyprctl, record, [this](std::optional<std::string> what) {
            if (!what)
                return;
            outcome = std::move(*what);
            if (!reviewing)
                showOverview();
        });
    }

    void WallpaperPage::addFromFiles() {
        const auto opened = files.chooseImage("Choose a wallpaper", [this](std::optional<std::filesystem::path> chosen) {
            if (!chosen)
                return;
            const auto environment = foundation::currentEnvironment();
            const auto added       = desktop::addWallpaper(*chosen, desktop::wallpaperFolder(environment));
            if (!added) {
                outcome = std::format("The image was not added: {}.", added.error());
                return showOverview();
            }
            selected = desktop::Wallpaper{*added, selected ? selected->fit : desktop::FitMode::cover};
            outcome  = std::format("Added to {}.", shortPath(added->parent_path()));
            check();
            if (const auto at = std::ranges::find(wallpapers, *added); at != wallpapers.end())
                pageStart = static_cast<std::size_t>(at - wallpapers.begin()) / pageSize * pageSize;
            tryOnDesktop();
            showOverview();
        });
        if (!opened) {
            outcome = std::format("The file chooser did not open: {}.", opened.error());
            showOverview();
        }
    }

    void WallpaperPage::undo() {
        const auto latest = configuration::latestChange(application::backupsFolder(foundation::currentEnvironment()), changeName);
        if (!latest)
            return showOverview();
        if (const auto undone = configuration::undoChange(*latest); !undone) {
            outcome = std::format("Nothing was undone: {}.", undone.error());
        } else {
            selected = desktop::currentWallpaper(foundation::currentEnvironment()).value_or(*selected);
            if (hyprpaper.running()) {
                outcome = "Undone, and hyprpaper is restarting to read the file as it was.";
                hyprpaper.restart();
            } else {
                outcome = "Undone.";
            }
        }
        showOverview();
    }

}
