// SPDX-License-Identifier: GPL-3.0-or-later
#include "ipc/events.hpp"

#include "ipc/socket.hpp"

#include <array>
#include <cerrno>
#include <format>

#include <fcntl.h>
#include <unistd.h>

namespace ipc {

    std::vector<Event> EventSplitter::feed(std::string_view chunk) {
        partial += chunk;

        std::vector<Event>     events;
        std::string_view       rest{partial};
        std::string_view::size_type end = 0;
        while ((end = rest.find('\n')) != std::string_view::npos) {
            const auto line = rest.substr(0, end);
            rest.remove_prefix(end + 1);

            const auto separator = line.find(">>");
            if (separator == std::string_view::npos)
                continue;
            events.push_back(Event{.name = std::string{line.substr(0, separator)}, .data = std::string{line.substr(separator + 2)}});
        }

        partial = std::string{rest};
        return events;
    }

    std::expected<EventConnection, std::string> EventConnection::open(const Instance& instance) {
        auto socket = connectTo(instance.eventSocket);
        if (!socket)
            return std::unexpected{socket.error()};

        const int flags = ::fcntl(socket->get(), F_GETFL);
        if (flags < 0 || ::fcntl(socket->get(), F_SETFL, flags | O_NONBLOCK) != 0)
            return std::unexpected{std::format("cannot make the event socket non-blocking: {}", lastError())};

        return EventConnection{std::move(*socket)};
    }

    std::expected<std::vector<Event>, std::string> EventConnection::drain() {
        std::vector<Event>     events;
        std::array<char, 8192> buffer{};
        while (true) {
            const auto count = ::read(socket.get(), buffer.data(), buffer.size());
            if (count > 0) {
                auto more = splitter.feed(std::string_view{buffer.data(), static_cast<std::size_t>(count)});
                events.insert(events.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
                continue;
            }
            if (count == 0)
                return std::unexpected{"Hyprland closed the event connection"};
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return events;
            if (errno != EINTR)
                return std::unexpected{std::format("the event connection failed: {}", lastError())};
        }
    }

}
