// SPDX-License-Identifier: GPL-3.0-or-later
#include "ipc/socket.hpp"

#include <algorithm>
#include <cerrno>
#include <format>
#include <system_error>

#include <sys/socket.h>
#include <sys/un.h>

namespace ipc {

    std::string lastError() {
        return std::system_category().message(errno);
    }

    std::expected<foundation::FileDescriptor, std::string> connectTo(const std::filesystem::path& path) {
        sockaddr_un address{};
        address.sun_family = AF_UNIX;

        const auto& text = path.native();
        if (text.size() >= sizeof(address.sun_path))
            return std::unexpected{std::format("the socket path is too long: {}", text)};
        std::ranges::copy(text, std::begin(address.sun_path));

        foundation::FileDescriptor socket{::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)};
        if (!socket.valid())
            return std::unexpected{std::format("cannot create a socket: {}", lastError())};

        // The sockets API takes every address family through the generic sockaddr type.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        if (::connect(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
            return std::unexpected{std::format("cannot connect to {}: {}", text, lastError())};

        return socket;
    }

}
