// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/screenshots.hpp"

#include "configuration/lua-binds.hpp"

#include <algorithm>
#include <format>

namespace desktop {

    std::string screenshotCommand(const Screenshots& screenshots, Shot shot) {
        const auto file = std::format("{}/$(date +%F_%H-%M-%S).png", screenshots.folder);
        std::string grim;
        switch (shot) {
            case Shot::area: grim = R"cmd(grim -g "$(slurp)")cmd"; break;
            case Shot::screens: grim = "grim"; break;
            case Shot::window: grim = R"cmd(grim -g "$(hyprctl activewindow -j | jq -r '"\(.at[0]),\(.at[1]) \(.size[0])x\(.size[1])"')")cmd"; break;
        }
        std::string save;
        if (screenshots.annotate)
            save = std::format("{} - | satty --filename - --output-filename {} --early-exit{}", grim, file, screenshots.copy ? " --copy-command wl-copy" : "");
        else if (screenshots.copy)
            save = std::format("{} - | tee {} | wl-copy", grim, file);
        else
            save = std::format("{} {}", grim, file);
        return std::format("mkdir -p {} && {}", screenshots.folder, save);
    }

    std::optional<Screenshots> screenshotsIn(std::string_view config) {
        const auto binds = configuration::luaBindLines(config);
        const auto print = std::ranges::find_if(binds, [](const configuration::LuaBindLine& bind) { return configuration::sameKeys(bind.keys, "Print"); });
        if (print == binds.end() || !print->command || !print->command->contains("grim"))
            return std::nullopt;
        const std::string_view command = *print->command;
        Screenshots            found;
        found.copy     = command.contains("wl-copy");
        found.annotate = command.contains("satty");
        // The folder of the file named with the date.
        if (const auto date = command.find("$(date"); date != std::string_view::npos) {
            const auto slash = command.rfind('/', date);
            const auto start = command.rfind(' ', date);
            if (slash != std::string_view::npos && start != std::string_view::npos && start < slash)
                found.folder = std::string{command.substr(start + 1, slash - start - 1)};
        }
        return found;
    }

    std::string withScreenshots(std::string_view config, const Screenshots& screenshots) {
        std::string text{config};
        for (const auto& key : shotKeys)
            text = configuration::withLuaExecBind(text, key.keys, screenshotCommand(screenshots, key.shot));
        return text;
    }

    std::string clipboardPicker(std::string_view menu) {
        return std::format("cliphist list | {} | cliphist decode | wl-copy", menu);
    }

    std::optional<std::string> clipboardKeysIn(std::string_view config) {
        for (const auto& bind : configuration::luaBindLines(config))
            if (bind.command && bind.command->contains("cliphist list"))
                return bind.keys;
        return std::nullopt;
    }

}
