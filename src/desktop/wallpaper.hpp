// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "configuration/file-change.hpp"
#include "configuration/main-config.hpp"
#include "desktop/waybar.hpp"
#include "foundation/environment.hpp"

#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace desktop {

    // How hyprpaper fits an image to the screen.
    enum class FitMode {
        cover,
        contain,
        fill,
        tile,
    };

    struct FitInfo {
        FitMode          mode;
        // As hyprpaper.conf writes it.
        std::string_view word;
        std::string_view name;
    };

    [[nodiscard]] std::span<const FitInfo> fitModes();

    struct Wallpaper {
        std::filesystem::path image;
        FitMode               fit = FitMode::cover;

        bool operator==(const Wallpaper&) const = default;
    };

    // The programs Lantern can draw the wallpaper with.
    enum class WallpaperProgram {
        // Hyprland's own, set through hyprpaper.conf.
        hyprpaper,
        // Fades from one image to the next, and remembers the last one itself.
        awww,
    };

    // The workspaces given a wallpaper of their own, by number.
    using WorkspaceWallpapers = std::map<int, Wallpaper>;

    // What the program needs: itself.
    [[nodiscard]] Requirement wallpaperRequirement(WallpaperProgram program, const foundation::Environment& environment);

    // The arguments that make `awww` show `wallpaper` on every monitor, fading in. awww
    // remembers it and shows it again when its daemon starts. Refuses the tile fit, which
    // awww does not have.
    [[nodiscard]] std::expected<std::vector<std::string>, std::string> awwwArguments(const Wallpaper& wallpaper);

    // The image `awww query -j` reports for the first monitor, if it shows one.
    [[nodiscard]] std::optional<std::filesystem::path> awwwImage(std::string_view queryJson);

    // $XDG_CONFIG_HOME/hypr/hyprpaper.conf, or ~/.config/hypr/hyprpaper.conf.
    [[nodiscard]] std::filesystem::path hyprpaperConfigFile(const foundation::Environment& environment);

    // The wallpaper hyprpaper.conf sets for every monitor without one of its own: the
    // wallpaper block whose monitor is empty. `home` expands a path that starts with ~.
    [[nodiscard]] std::optional<Wallpaper> fallbackWallpaper(std::string_view config, const std::filesystem::path& home);

    // The monitors hyprpaper.conf gives a wallpaper of their own, which the fallback does not change.
    [[nodiscard]] std::vector<std::string> monitorsWithOwnWallpaper(std::string_view config);

    // `config` with the fallback block's path and fit_mode set to `wallpaper`, and every
    // other byte as it was. A block is added at the end when there is none. Refuses the
    // format from before hyprpaper 0.8, which used `wallpaper = monitor,path` lines.
    [[nodiscard]] std::expected<std::string, std::string> withFallbackWallpaper(std::string_view config, const Wallpaper& wallpaper);

    // The change to hyprpaper.conf that sets `wallpaper` for every monitor.
    [[nodiscard]] std::expected<configuration::FileChange, std::string> planWallpaper(const Wallpaper& wallpaper, const foundation::Environment& environment);

    // The arguments that make `hyprctl` show `wallpaper` on every monitor at once, through
    // hyprpaper's own socket, without writing anything. Refuses a path with a comma,
    // which hyprctl would split.
    [[nodiscard]] std::expected<std::vector<std::string>, std::string> showWallpaperArguments(const Wallpaper& wallpaper);

    // The workspaces the Lua config gives a wallpaper of their own, from the block
    // workspaceWallpaperBlock writes.
    [[nodiscard]] WorkspaceWallpapers workspaceWallpapersIn(std::string_view config);

    // The wallpaper that block shows on every other workspace.
    [[nodiscard]] std::optional<Wallpaper> restWallpaperIn(std::string_view config);

    // The Lua that has `program` show each workspace's wallpaper whenever it becomes active,
    // and `rest` on every other workspace, on the workspace's monitor. Refuses a path with a
    // quote or a backslash, and with hyprpaper, a comma.
    [[nodiscard]] std::expected<std::string, std::string> workspaceWallpaperBlock(WallpaperProgram program, const Wallpaper& rest, const WorkspaceWallpapers& workspaces);

    // The command that starts the program at login.
    [[nodiscard]] std::string_view loginCommand(WallpaperProgram program);

    // Every file change that makes `program` the one wallpaper program, started at login,
    // with `wallpaper`: the Hyprland config starts it and no longer starts the other one,
    // turns off Hyprland's own wallpaper and logo where it sets them, and with hyprpaper,
    // hyprpaper.conf sets the wallpaper. With `workspaces`, a Lua config gets the block
    // that shows theirs, and without, loses it. A config that is a link is followed, and
    // the file it points at is the one changed; the link stays. Empty when nothing needs to
    // change.
    [[nodiscard]] std::expected<std::vector<configuration::FileChange>, std::string> planWallpaperSetup(WallpaperProgram program, const Wallpaper& wallpaper,
                                                                                                       const WorkspaceWallpapers&       workspaces,
                                                                                                       const configuration::MainConfig& config,
                                                                                                       const foundation::Environment&   environment);

    // The fallback wallpaper hyprpaper.conf sets now.
    [[nodiscard]] std::optional<Wallpaper> currentWallpaper(const foundation::Environment& environment);

    // The image on the desktop now: the one awww shows, or the one hyprpaper.conf sets. It
    // asks awww, so it waits as long as `awww query` takes.
    [[nodiscard]] std::optional<std::filesystem::path> shownWallpaperImage(const foundation::Environment& environment);

    // Where the user's wallpapers are kept: Wallpapers in the pictures folder, which
    // ~/.config/user-dirs.dirs may name (XDG_PICTURES_DIR), or ~/Pictures.
    [[nodiscard]] std::filesystem::path wallpaperFolder(const foundation::Environment& environment);

    // Copies `image` into `folder` under a new name, a random UUID with the image's own
    // extension, and returns the copy. An image already in the folder, or one whose exact
    // copy is there, is not copied again.
    [[nodiscard]] std::expected<std::filesystem::path, std::string> addWallpaper(const std::filesystem::path& image, const std::filesystem::path& folder);

    // Images in the folders wallpapers are kept in: the user's wallpaper folder (see
    // wallpaperFolder) and ~/.local/share/backgrounds, and then `systemFolders` and the
    // folders in them. Other pictures, such as screenshots, are not wallpapers until added. In a folder called hypr only Hyprland's own wall*.png count.
    [[nodiscard]] std::vector<std::filesystem::path> findWallpapers(const foundation::Environment&     environment,
                                                                    std::span<const std::filesystem::path> systemFolders);

}
