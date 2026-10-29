// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/location.hpp"

#include <format>

namespace configuration {

    std::string Location::brief() const {
        return std::format("{}:{}", file.filename().string(), line);
    }

}
