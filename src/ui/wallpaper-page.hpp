// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "application/file-chooser.hpp"
#include "application/settings.hpp"
#include "configuration/file-change.hpp"
#include "desktop/wallpaper.hpp"
#include "ui/program-switch.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/Image.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    // The wallpaper. Lantern draws none of its own: it uses hyprpaper, Hyprland's own
    // wallpaper program, or awww, which fades between images. It shows the images it finds
    // to choose from and tries them on the desktop. With hyprpaper it writes the choice to
    // hyprpaper.conf only after the user reviews the change; awww remembers it itself.
    class WallpaperPage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        WallpaperPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Looks again, as after the user installed something or added images.
        void check();

        // Stops watching hyprpaper. Called before the event loop goes away.
        void close();

        // Draws the page again, after Lantern's colors changed.
        void restyle();

        // The page is no longer shown: a wallpaper tried on the desktop and not kept is
        // replaced by the one hyprpaper.conf sets. When Lantern is `closing` and the file
        // sets none, it is left: restarting hyprpaper would not finish.
        void leave(bool closing = false);

      private:
        void                                                                   showOverview();
        void                                                                   showReview();
        void                                                                   apply();
        // Asks Hyprland, once it has read the changed config, whether it found errors, and
        // puts the files of `record` back if it did.
        void                                                                   checkConfig(const std::filesystem::path& record);
        // Opens the desktop's file chooser, and copies the image chosen into the wallpapers folder.
        void                                                                   addFromFiles();
        void                                                                   choose(desktop::WallpaperProgram chosen);
        // Asks awww which image it shows.
        void                                                                   askAwww();
        void                                                                   showSwitch();
        [[nodiscard]] ProgramSwitch&                                           current();
        // The wallpaper the chosen program keeps: hyprpaper.conf's, or what awww shows.
        [[nodiscard]] std::optional<desktop::Wallpaper>                        kept() const;
        // Shows the chosen wallpaper on the desktop through hyprpaper, writing nothing.
        void                                                                   tryOnDesktop();
        void                                                                   undo();
        // Moves the chosen image to the trash, unless it is the system's or the kept wallpaper.
        void                                                                   trashSelected();
        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> chooser();
        // After a click on an image or a fit: changes only what shows the selection, so
        // nothing else is made anew. hyprtoolkit draws a new text or image a frame or two
        // after it is made, so rebuilding the page on every click made it flicker.
        void                                                                   updateSelection();
        void                                                                   fillStatus();
        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> grid();
        // Makes the cards for the row shown.
        void                                                                   fillGrid();
        void                                                                   scroll(Hyprtoolkit::Input::eAxisAxis axis, float delta);
        // Reads the wallpapers hyprland.lua gives workspaces of their own.
        void                                                                   readWorkspaces();
        // Makes the workspace buttons: every workspace, and each by number with its image.
        void                                                                   fillWorkspaces();
        // Chooses the wallpaper of workspace `number`, or with none, of every workspace
        // without one of its own.
        void                                                                   pickWorkspace(std::optional<int> number);
        // After a click on an image or a fit: the workspace being chosen for keeps it.
        void                                                                   keepForWorkspace();
        // Shows the active workspace's wallpaper on its monitor, once it has one of its own.
        void                                                                   showActiveWorkspace();
        // The wallpaper of every workspace without one of its own.
        [[nodiscard]] desktop::Wallpaper                                       mainWallpaper() const;
        [[nodiscard]] desktop::PerWorkspace                                    perWorkspace() const { return {workspaces, randomFrom}; }
        // Turns a random wallpaper for each workspace on, from the wallpapers folder, or off.
        void                                                                   toggleRandom();
        void                                                                   chooseRandomFolder();
        // Chooses a new random image for every workspace, and shows the active ones.
        void                                                                   shuffle();
        // In place of the chooser and the images, with random wallpapers: the folder and the fit.
        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> randomPanel();
        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> thumbnail(const std::filesystem::path& image);

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        application::EventLoop                                               loop;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> body;
        // The page's action buttons, at the bottom right.
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    footer;
        RowList                                                              diff;
        ProgramSwitch                                                        hyprpaper;
        ProgramSwitch                                                        awww;
        application::FileChooser                                             files;
        // An image to give awww once its daemon, just started by Apply, is running.
        std::optional<desktop::Wallpaper>                                    awwwAfterStart;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> switchArea;
        desktop::WallpaperProgram                                            program = desktop::WallpaperProgram::hyprpaper;
        // awww's command, and the image it shows with the fit last kept.
        std::optional<std::filesystem::path>                                 awwwCommand;
        std::optional<desktop::Wallpaper>                                    awwwShows;
        application::Settings                                                settings;

        std::vector<std::filesystem::path>                                   wallpapers;
        // The first image on the grid's page, and how many a page shows.
        std::size_t                                                          pageStart = 0;
        std::size_t                                                          pageSize  = 8;
        std::optional<desktop::Wallpaper>                                    selected;
        bool                                                                 reviewing = false;
        // The desktop shows the chosen wallpaper, which hyprpaper.conf does not set yet.
        bool                                                                 tried     = false;
        std::optional<std::filesystem::path>                                 hyprctl;
        // Images already loaded, kept so that choosing again does not load them anew.
        std::map<std::filesystem::path, Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CImageElement>> thumbnails;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CImageElement>        preview;
        std::filesystem::path                                                previewImage;
        // What shows the selection, kept so that a click changes it in place.
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         nameText;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         folderText;
        std::vector<std::pair<desktop::FitMode, Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement>>> fitButtons;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> status;
        // What the status shows now, so that it is made anew only when that changes.
        std::string                                                          statusShown;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         gridTitle;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> gridRows;
        std::map<std::filesystem::path, Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRectangleElement>> cards;
        std::vector<configuration::FileChange>                               changes;
        desktop::WorkspaceWallpapers                                         workspaces;
        // The workspace whose wallpaper is being chosen, and meanwhile, every other one's.
        std::optional<int>                                                   workspace;
        std::optional<desktop::Wallpaper>                                    everyOther;
        // What hyprland.lua shows on workspaces without their own, when it gives any theirs.
        std::optional<desktop::Wallpaper>                                    configRest;
        bool                                                                 luaConfig = false;
        std::optional<std::filesystem::path>                                 randomFrom;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    workspaceRow;
        // Beside Move to trash, so that a workspace can go back to every workspace's.
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    selectionActions;
        bool                                                                 ownShown = false;
        std::map<std::filesystem::path, Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CImageElement>> workspaceThumbnails;
        std::string                                                          outcome;
    };

}
