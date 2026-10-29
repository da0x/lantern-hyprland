// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "configuration/location.hpp"
#include "configuration/value.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace configuration {

    // One call to hl.* that the config made while it was read.
    struct Call {
        std::string           function;
        std::vector<Value>    arguments;
        std::string           submap;

        // Every frame of the config's own code, innermost first: a binding made in a loop
        // inside a function has the loop's line first and the function's caller after it.
        std::vector<Location> stack;

        [[nodiscard]] Location location() const { return stack.empty() ? Location{} : stack.front(); }
    };

    // Something the config tried that Lantern does not do while reading it, such as
    // starting a program or writing a file.
    struct Refusal {
        std::string           what;
        std::vector<Location> stack;
    };

    // Why reading stopped before the end of the config.
    struct Failure {
        std::string           message;
        std::vector<Location> stack;
    };

    // Everything the config did, in order. A config that failed partway still has every
    // call it made before the failure.
    struct Recording {
        std::vector<Call>                  calls;
        std::vector<Refusal>               refused;
        std::vector<std::filesystem::path> files;
        std::optional<Failure>             failure;
    };

    // Runs the config in the sandboxed recording interpreter. Nothing it does takes
    // effect. An error here means the interpreter itself could not run; a config that
    // fails is a Recording with a failure.
    [[nodiscard]] std::expected<Recording, std::string> record(const std::filesystem::path& mainFile);

}
