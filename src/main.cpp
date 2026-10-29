// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/main-window.hpp"

#include <print>
#include <span>
#include <string_view>

int main(int argc, char** argv) {
    ui::Options options;
    const std::span arguments{argv, static_cast<std::size_t>(argc)};
    for (std::size_t index = 1; index < arguments.size(); ++index) {
        const std::string_view argument{arguments[index]};
        if (argument == "--verbose") {
            options.verbose = true;
        } else if (argument == "--page" && index + 1 < arguments.size()) {
            options.page = arguments[++index];
        } else if (argument == "--help") {
            std::println("Usage: lantern-hyprland [--verbose] [--page TITLE]");
            return 0;
        } else {
            std::println(stderr, "lantern-hyprland: unknown option {}", argument);
            return 2;
        }
    }
    return ui::run(options);
}
