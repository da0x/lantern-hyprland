// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "activation/effects.hpp"

#include <sys/types.h>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace activation {

    // A program Lantern started, by the process it became and the name it had then, so
    // that a process with the same number later is never mistaken for it.
    struct StartedProgram {
        pid_t       process = 0;
        std::string name;
        std::string command;
    };

    // What is active now. Kept in Lantern's folder while anything is, so that
    // Deactivate works after Lantern was closed, or stopped without warning.
    struct Record {
        std::vector<Area>           areas;
        std::vector<StartedProgram> programs;
        // When it was activated, as the clock read it.
        std::string                 since;
    };

    [[nodiscard]] std::optional<Record>            loadRecord(const std::filesystem::path& lanternFolder);
    [[nodiscard]] std::expected<void, std::string> saveRecord(const Record& record, const std::filesystem::path& lanternFolder);
    void                                           removeRecord(const std::filesystem::path& lanternFolder);

}
