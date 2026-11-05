// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

struct sd_bus;
struct sd_bus_slot;
struct sd_bus_message;
struct sd_bus_error;

namespace application {

    // The desktop's own file chooser, through xdg-desktop-portal on the session bus, so it
    // looks like every other app's. hyprtoolkit has none of its own.
    class FileChooser {
      public:
        // The chosen path, nothing if the user canceled, or why the chooser did not open.
        using Chosen = std::expected<std::optional<std::filesystem::path>, std::string>;

        explicit FileChooser(EventLoop loop);
        ~FileChooser();
        FileChooser(const FileChooser&)            = delete;
        FileChooser& operator=(const FileChooser&) = delete;

        // Asks for one image and returns at once. `done` runs on the event loop with the
        // chosen file, nothing if the user canceled, or the portal's error. Fails at once
        // when the session bus cannot be reached.
        [[nodiscard]] std::expected<void, std::string> chooseImage(const std::string& title, std::function<void(Chosen)> done);

        // The same for a folder.
        [[nodiscard]] std::expected<void, std::string> chooseFolder(const std::string& title, std::function<void(Chosen)> done);

      private:
        [[nodiscard]] std::expected<void, std::string> open(const std::string& title, bool folder, std::function<void(Chosen)> done);
        static int                                          answered(sd_bus_message* message, void* self, sd_bus_error* error);
        static int                                          replied(sd_bus_message* message, void* self, sd_bus_error* error);
        void                                                finish(Chosen chosen);

        EventLoop                                           loop;
        sd_bus*                                             bus      = nullptr;
        sd_bus_slot*                                        response = nullptr;
        int                                                 watched  = -1;
        int                                                 requests = 0;
        std::function<void(Chosen)> waiting;
    };

    // The path in a file:// URI, with its %-escapes decoded.
    [[nodiscard]] std::optional<std::filesystem::path> pathOfUri(std::string_view uri);

}
