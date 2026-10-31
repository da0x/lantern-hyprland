// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/lantern-folder.hpp"

namespace application {

    std::filesystem::path lanternFolder(const foundation::Environment& environment) {
        return foundation::lanternFolder(environment);
    }

    void moveOldFolder(const foundation::Environment& environment) {
        const auto      folder = lanternFolder(environment);
        const auto      old    = folder.parent_path() / "hyprland-studio";
        std::error_code error;
        if (std::filesystem::is_directory(old, error) && !std::filesystem::exists(folder, error))
            std::filesystem::rename(old, folder, error);
    }

}
