// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/environment.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace desktop {

    // A desktop setting in dconf, which GTK apps and the desktop portal read: what it is now
    // and what Lantern wants, both as gsettings prints them ('prefer-dark', 1.25, true).
    struct GSetting {
        std::string schema;
        std::string key;
        std::string current;
        std::string wanted;
    };

    // A setting's value as gsettings prints it, or nothing when gsettings is not installed
    // or does not know the key.
    [[nodiscard]] std::optional<std::string> gsetting(const std::string& schema, const std::string& key, const foundation::Environment& environment);

    // Of the settings wanted, as {schema, key, value} with `current` left empty, those that
    // differ from what is set now, with their current values. Numbers are compared as
    // numbers, so 1 and 1.0 are the same. Empty when gsettings is not installed.
    [[nodiscard]] std::vector<GSetting> gsettingChanges(std::vector<GSetting> wanted, const foundation::Environment& environment);

    // Sets each setting, after keeping its old value in the change's record for undo.
    void setGsettings(const std::vector<GSetting>& settings, const std::filesystem::path& record, const foundation::Environment& environment);

    // Puts back the settings a record kept, if it kept any.
    void restoreGsettings(const std::filesystem::path& record, const foundation::Environment& environment);

}
