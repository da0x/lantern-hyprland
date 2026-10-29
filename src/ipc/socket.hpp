// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/file-descriptor.hpp"

#include <expected>
#include <filesystem>
#include <string>

namespace ipc {

    // Connects a blocking stream socket to a Unix socket path.
    [[nodiscard]] std::expected<foundation::FileDescriptor, std::string> connectTo(const std::filesystem::path& path);

    // The text of the last system error, for messages.
    [[nodiscard]] std::string lastError();

}
