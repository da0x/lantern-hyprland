// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/file-descriptor.hpp"
#include "ipc/instance.hpp"

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace ipc {

    // One line from the event socket: "name>>data".
    struct Event {
        std::string name;
        std::string data;

        friend bool operator==(const Event&, const Event&) = default;
    };

    // Splits the event stream into events. Reads arrive in arbitrary pieces, so a line cut in
    // two is kept until the rest of it arrives.
    class EventSplitter {
      public:
        [[nodiscard]] std::vector<Event> feed(std::string_view chunk);

      private:
        std::string partial;
    };

    // A connection to the event socket that never blocks.
    class EventConnection {
      public:
        [[nodiscard]] static std::expected<EventConnection, std::string> open(const Instance& instance);

        [[nodiscard]] int descriptor() const { return socket.get(); }

        // Reads everything available now. Hyprland drops a client that falls 64 events behind,
        // so call this every time the descriptor is readable, and drain it completely.
        // An error means the connection is gone.
        [[nodiscard]] std::expected<std::vector<Event>, std::string> drain();

      private:
        explicit EventConnection(foundation::FileDescriptor socket) : socket{std::move(socket)} {}

        foundation::FileDescriptor socket;
        EventSplitter              splitter;
    };

}
