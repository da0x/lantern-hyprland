// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/wallpaper.hpp"
#include "configuration/lua-edit.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include <unistd.h>

namespace {

    using desktop::FitMode;
    using desktop::Wallpaper;

    TEST(Wallpaper, ChangesOnlyTheFallbackBlocksValues) {
        const std::string config = "# my wallpapers\n"
                                   "splash = false\n"
                                   "\n"
                                   "wallpaper {\n"
                                   "  monitor = DP-1\n"
                                   "  path = ~/left.png   # the left one\n"
                                   "}\n"
                                   "\n"
                                   "wallpaper {\n"
                                   "  monitor =\n"
                                   "  path    =  ~/old.png\n"
                                   "  fit_mode = cover\n"
                                   "}\n";
        const auto changed = desktop::withFallbackWallpaper(config, Wallpaper{"/home/me/new.jpg", FitMode::contain});
        ASSERT_TRUE(changed.has_value());
        EXPECT_EQ(*changed, "# my wallpapers\n"
                            "splash = false\n"
                            "\n"
                            "wallpaper {\n"
                            "  monitor = DP-1\n"
                            "  path = ~/left.png   # the left one\n"
                            "}\n"
                            "\n"
                            "wallpaper {\n"
                            "  monitor =\n"
                            "  path    =  /home/me/new.jpg\n"
                            "  fit_mode = contain\n"
                            "}\n");
        EXPECT_EQ(desktop::monitorsWithOwnWallpaper(config), std::vector<std::string>{"DP-1"});
        EXPECT_EQ(desktop::fallbackWallpaper(config, "/home/me"), (Wallpaper{"/home/me/old.png", FitMode::cover}));
        EXPECT_EQ(desktop::fallbackWallpaper(*changed, "/home/me"), (Wallpaper{"/home/me/new.jpg", FitMode::contain}));
    }

    TEST(Wallpaper, AddsAMissingFitModeWithTheBlocksIndentation) {
        const auto changed = desktop::withFallbackWallpaper("wallpaper {\n\tpath = /a.png\n}\n", Wallpaper{"/b.png", FitMode::tile});
        ASSERT_TRUE(changed.has_value());
        EXPECT_EQ(*changed, "wallpaper {\n\tpath = /b.png\n\tfit_mode = tile\n}\n");
    }

    TEST(Wallpaper, AddsABlockWhenThereIsNone) {
        EXPECT_EQ(desktop::withFallbackWallpaper("", Wallpaper{"/a.png", FitMode::cover}), "wallpaper {\n    monitor =\n    path = /a.png\n    fit_mode = cover\n}\n");
        EXPECT_EQ(desktop::withFallbackWallpaper("splash = false", Wallpaper{"/a.png", FitMode::fill}),
                  "splash = false\n\nwallpaper {\n    monitor =\n    path = /a.png\n    fit_mode = fill\n}\n");
    }

    TEST(Wallpaper, RefusesTheFormatBeforeHyprpaper08) {
        EXPECT_FALSE(desktop::withFallbackWallpaper("preload = /a.png\nwallpaper = ,/a.png\n", Wallpaper{"/b.png", FitMode::cover}).has_value());
    }

    TEST(Wallpaper, FindsImagesInTheUsualFolders) {
        static std::atomic<int> counter = 0;
        const auto root = std::filesystem::temp_directory_path() / ("lantern-hyprland-wallpaper-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
        const auto make = [&root](const std::string& relative) {
            std::filesystem::create_directories((root / relative).parent_path());
            std::ofstream{root / relative} << "x";
        };
        make("home/Pictures/Wallpapers/b.png");
        make("home/Pictures/Wallpapers/forest/a.JPG");
        make("home/Pictures/notes.txt");
        make("home/Pictures/photo.webp");
        make("share/hypr/wall0.png");
        make("share/hypr/lockdead.png");
        make("share/backgrounds/sway/blue.png");

        const std::array system{root / "share" / "hypr", root / "share" / "backgrounds"};
        const auto       found = desktop::findWallpapers({{"HOME", (root / "home").string()}}, system);
        const std::vector<std::filesystem::path> expected{
            root / "home/Pictures/Wallpapers/b.png", root / "home/Pictures/Wallpapers/forest/a.JPG",
            root / "share/hypr/wall0.png",           root / "share/backgrounds/sway/blue.png",
        };
        EXPECT_EQ(found, expected);
        std::filesystem::remove_all(root);
    }

    TEST(Wallpaper, AsksHyprctlInItsOwnWords) {
        EXPECT_EQ(desktop::showWallpaperArguments(Wallpaper{"/a b/c.png", FitMode::fill}), (std::vector<std::string>{"hyprpaper", "wallpaper", ", /a b/c.png, stretch"}));
        EXPECT_FALSE(desktop::showWallpaperArguments(Wallpaper{"/a,b.png", FitMode::cover}).has_value());
    }

    TEST(Wallpaper, AsksAwwwToFadeIn) {
        EXPECT_EQ(desktop::awwwArguments(Wallpaper{"/a.png", FitMode::contain}),
                  (std::vector<std::string>{"img", "/a.png", "--resize", "fit", "--transition-type", "fade"}));
        EXPECT_FALSE(desktop::awwwArguments(Wallpaper{"/a.png", FitMode::tile}).has_value());
    }

    TEST(Wallpaper, ReadsWhatAwwwShows) {
        const auto shown = R"({"": [{"name": "DP-10", "width": 3840, "height": 2160, "scale": 1.0, "displaying": {"image": "/usr/share/hypr/wall2.png"}}]})";
        EXPECT_EQ(desktop::awwwImage(shown), std::filesystem::path{"/usr/share/hypr/wall2.png"});
        EXPECT_FALSE(desktop::awwwImage(R"({"": [{"name": "DP-10", "displaying": {"color": "#000000"}}]})").has_value());
        EXPECT_FALSE(desktop::awwwImage("not json").has_value());
    }

    TEST(Wallpaper, KeepsAddedImagesInThePicturesFolder) {
        static std::atomic<int> counter = 0;
        const auto root = std::filesystem::temp_directory_path() / ("lantern-hyprland-add-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
        const auto write = [&root](const std::string& relative, const std::string& text) {
            std::filesystem::create_directories((root / relative).parent_path());
            std::ofstream{root / relative} << text;
            return root / relative;
        };
        write("home/.config/user-dirs.dirs", "# from xdg-user-dirs\nXDG_PICTURES_DIR=\"$HOME/Bilder\"\n");
        const foundation::Environment environment{{"HOME", (root / "home").string()}};
        const auto folder = desktop::wallpaperFolder(environment);
        EXPECT_EQ(folder, root / "home/Bilder/Wallpapers");

        const auto first = write("downloads/sea.png", "sea");
        const auto added = desktop::addWallpaper(first, folder);
        ASSERT_TRUE(added.has_value()) << added.error();
        EXPECT_EQ(added->parent_path(), folder);
        EXPECT_EQ(added->extension(), ".png");
        // A random UUID: 8-4-4-4-12 hexadecimal digits, version 4.
        const auto name = added->stem().string();
        ASSERT_EQ(name.size(), 36U);
        EXPECT_EQ(name[8], '-');
        EXPECT_EQ(name[14], '4');
        EXPECT_TRUE(std::ranges::all_of(name, [](char character) { return character == '-' || std::isxdigit(static_cast<unsigned char>(character)) != 0; }));

        // The same image again is not copied twice, wherever it comes from.
        EXPECT_EQ(desktop::addWallpaper(first, folder), *added);
        EXPECT_EQ(desktop::addWallpaper(write("elsewhere/copy.png", "sea"), folder), *added);
        // A different image with the same name gets a name of its own.
        const auto other = desktop::addWallpaper(write("elsewhere/sea.png", "another sea"), folder);
        ASSERT_TRUE(other.has_value());
        EXPECT_NE(*other, *added);
        // One already in the folder stays where it is.
        EXPECT_EQ(desktop::addWallpaper(*added, folder), *added);
        std::filesystem::remove_all(root);
    }

    TEST(Wallpaper, PlansTheWholeSetupThroughALinkedConfig) {
        static std::atomic<int> counter = 0;
        const auto root = std::filesystem::temp_directory_path() / ("lantern-hyprland-setup-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
        std::filesystem::create_directories(root / "dotfiles");
        std::filesystem::create_directories(root / "home/.config/hypr");
        std::ofstream{root / "dotfiles/hyprland.lua"} << "hl.on(\"hyprland.start\", function ()\n    hl.exec_cmd(\"awww-daemon\")\nend)\n";
        std::filesystem::create_symlink(root / "dotfiles/hyprland.lua", root / "home/.config/hypr/hyprland.lua");
        const foundation::Environment     environment{{"HOME", (root / "home").string()}};
        const configuration::MainConfig    config{.file = root / "home/.config/hypr/hyprland.lua", .choice = configuration::Choice::search, .safeMode = false};

        const auto planned = desktop::planWallpaperSetup(desktop::WallpaperProgram::hyprpaper, Wallpaper{"/usr/share/hypr/wall2.png", FitMode::cover}, {}, config, environment);
        ASSERT_TRUE(planned.has_value()) << planned.error();
        ASSERT_EQ(planned->size(), 2U);
        EXPECT_EQ((*planned)[0].file, root / "home/.config/hypr/hyprpaper.conf");
        // The link's target is changed, and the link itself is left alone.
        EXPECT_EQ((*planned)[1].file, std::filesystem::canonical(root / "dotfiles/hyprland.lua"));
        EXPECT_EQ((*planned)[1].proposed, "hl.on(\"hyprland.start\", function ()\n    hl.exec_cmd(\"hyprpaper\")\nend)\n");

        // awww needs only the config, and with it already started, nothing at all.
        const auto withAwww = desktop::planWallpaperSetup(desktop::WallpaperProgram::awww, Wallpaper{"/a.png", FitMode::cover}, {}, config, environment);
        ASSERT_TRUE(withAwww.has_value());
        EXPECT_TRUE(withAwww->empty());

        // Workspaces of their own add the block that shows them, which reads back.
        const desktop::WorkspaceWallpapers workspaces{{1, Wallpaper{"/one.png", FitMode::cover}}, {3, Wallpaper{"/three.jpg", FitMode::contain}}};
        const auto withWorkspaces = desktop::planWallpaperSetup(desktop::WallpaperProgram::awww, Wallpaper{"/a.png", FitMode::fill}, {workspaces, {}}, config, environment);
        ASSERT_TRUE(withWorkspaces.has_value()) << withWorkspaces.error();
        ASSERT_EQ(withWorkspaces->size(), 1U);
        const auto& proposed = (*withWorkspaces)[0].proposed;
        EXPECT_EQ(desktop::perWorkspaceIn(proposed), (desktop::PerWorkspace{workspaces, {}}));
        EXPECT_EQ(desktop::restWallpaperIn(proposed), (Wallpaper{"/a.png", FitMode::fill}));
        std::ofstream{root / "dotfiles/hyprland.lua"} << proposed;
        // And without them, the block goes, leaving the config as it was.
        const auto without = desktop::planWallpaperSetup(desktop::WallpaperProgram::awww, Wallpaper{"/a.png", FitMode::fill}, {}, config, environment);
        ASSERT_TRUE(without.has_value());
        ASSERT_EQ(without->size(), 1U);
        EXPECT_EQ((*without)[0].proposed, "hl.on(\"hyprland.start\", function ()\n    hl.exec_cmd(\"awww-daemon\")\nend)\n");
        std::filesystem::remove_all(root);
    }

    TEST(Wallpaper, ShowsEachWorkspacesWallpaperWhenItBecomesActive) {
        const desktop::PerWorkspace own{{{2, Wallpaper{"/home/me/sea.png", FitMode::cover}}}, {}};
        const auto awww = desktop::workspaceWallpaperBlock(desktop::WallpaperProgram::awww, Wallpaper{"/rest.png", FitMode::contain}, own);
        ASSERT_TRUE(awww.has_value());
        EXPECT_TRUE(awww->contains(R"(local rest = { "/rest.png", "fit" })"));
        EXPECT_TRUE(awww->contains(R"([2] = { "/home/me/sea.png", "crop" },)"));
        EXPECT_TRUE(awww->contains("hl.on(\"workspace.active\", showOn)"));
        EXPECT_TRUE(awww->contains("awww img -o"));
        const auto hyprpaper = desktop::workspaceWallpaperBlock(desktop::WallpaperProgram::hyprpaper, Wallpaper{"/rest.png", FitMode::tile}, own);
        ASSERT_TRUE(hyprpaper.has_value());
        EXPECT_TRUE(hyprpaper->contains(R"(local rest = { "/rest.png", "tile" })"));
        EXPECT_TRUE(hyprpaper->contains("hyprctl hyprpaper wallpaper"));
        // What the shell or hyprctl would split or end early is refused.
        EXPECT_FALSE(desktop::workspaceWallpaperBlock(desktop::WallpaperProgram::awww, Wallpaper{"/it's.png", FitMode::cover}, own).has_value());
        EXPECT_FALSE(desktop::workspaceWallpaperBlock(desktop::WallpaperProgram::hyprpaper, Wallpaper{"/a,b.png", FitMode::cover}, own).has_value());
        EXPECT_FALSE(desktop::workspaceWallpaperBlock(desktop::WallpaperProgram::awww, Wallpaper{"/rest.png", FitMode::tile}, own).has_value());

        // From a folder, the block reads back as that folder, with the fit of the rest.
        const desktop::PerWorkspace random{{}, "/home/me/Pictures/Wallpapers"};
        const auto fromFolder = desktop::workspaceWallpaperBlock(desktop::WallpaperProgram::awww, Wallpaper{"/rest.png", FitMode::cover}, random);
        ASSERT_TRUE(fromFolder.has_value());
        const auto config = configuration::withLuaBlock("", "a wallpaper for each workspace", *fromFolder, true);
        EXPECT_EQ(desktop::perWorkspaceIn(config), random);
        EXPECT_EQ(desktop::restWallpaperIn(config), (Wallpaper{"/rest.png", FitMode::cover}));
    }

    TEST(Wallpaper, ShufflesADifferentImageOntoEachWorkspace) {
        const std::vector<std::filesystem::path> images{"/a.png", "/b.png", "/c.png"};
        const std::vector<int>                   three{1, 2, 3};
        const auto                               chosen = desktop::shuffledWorkspaces(three, images, 7);
        ASSERT_EQ(chosen.size(), 3U);
        std::set<std::filesystem::path> different;
        for (const auto& [number, image] : chosen)
            different.insert(image);
        EXPECT_EQ(different.size(), 3U);
        // More workspaces than images: every one still gets one.
        const std::vector<int> five{1, 2, 3, 4, 5};
        EXPECT_EQ(desktop::shuffledWorkspaces(five, images, 7).size(), 5U);
        EXPECT_TRUE(desktop::shuffledWorkspaces(five, {}, 7).empty());

        // The session's choices are kept as a number, a tab and a path on each line.
        EXPECT_EQ(desktop::randomChoicesIn(desktop::randomChoicesText(chosen)), chosen);
        EXPECT_EQ(desktop::randomChoicesIn("1\t/a b.png\nnot a line\n12\t/c.png\n"), (std::map<int, std::filesystem::path>{{1, "/a b.png"}, {12, "/c.png"}}));
        EXPECT_EQ(desktop::randomChoicesFile(foundation::Environment{{"XDG_RUNTIME_DIR", "/run/user/1000"}}), "/run/user/1000/lantern-workspace-wallpapers");
    }

}
