// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/wallpaper.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <atomic>
#include <filesystem>
#include <fstream>
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
        const auto withWorkspaces = desktop::planWallpaperSetup(desktop::WallpaperProgram::awww, Wallpaper{"/a.png", FitMode::fill}, workspaces, config, environment);
        ASSERT_TRUE(withWorkspaces.has_value()) << withWorkspaces.error();
        ASSERT_EQ(withWorkspaces->size(), 1U);
        const auto& proposed = (*withWorkspaces)[0].proposed;
        EXPECT_EQ(desktop::workspaceWallpapersIn(proposed), workspaces);
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
        const desktop::WorkspaceWallpapers workspaces{{2, Wallpaper{"/home/me/sea.png", FitMode::cover}}};
        const auto awww = desktop::workspaceWallpaperBlock(desktop::WallpaperProgram::awww, Wallpaper{"/rest.png", FitMode::contain}, workspaces);
        ASSERT_TRUE(awww.has_value());
        EXPECT_EQ(*awww, R"(do
    local wallpapers = {
        [2] = { "/home/me/sea.png", "crop" },
    }
    local rest = { "/rest.png", "fit" }
    hl.on("workspace.active", function(workspace)
        if not workspace.monitor then
            return
        end
        local wallpaper = workspace.id and wallpapers[workspace.id] or rest
        hl.exec_cmd("awww img -o " .. workspace.monitor.name .. " --resize " .. wallpaper[2] .. " --transition-type fade '" .. wallpaper[1] .. "'")
    end)
end
)");
        const auto hyprpaper = desktop::workspaceWallpaperBlock(desktop::WallpaperProgram::hyprpaper, Wallpaper{"/rest.png", FitMode::tile}, workspaces);
        ASSERT_TRUE(hyprpaper.has_value());
        EXPECT_TRUE(hyprpaper->contains(R"(local rest = { "/rest.png", "tile" })"));
        EXPECT_TRUE(hyprpaper->contains("hyprctl hyprpaper wallpaper"));
        // What the shell or hyprctl would split or end early is refused.
        EXPECT_FALSE(desktop::workspaceWallpaperBlock(desktop::WallpaperProgram::awww, Wallpaper{"/it's.png", FitMode::cover}, workspaces).has_value());
        EXPECT_FALSE(desktop::workspaceWallpaperBlock(desktop::WallpaperProgram::hyprpaper, Wallpaper{"/a,b.png", FitMode::cover}, workspaces).has_value());
        EXPECT_FALSE(desktop::workspaceWallpaperBlock(desktop::WallpaperProgram::awww, Wallpaper{"/rest.png", FitMode::tile}, workspaces).has_value());
    }

}
