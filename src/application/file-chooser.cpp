// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/file-chooser.hpp"

#include <systemd/sd-bus.h>

#include <chrono>
#include <cstring>
#include <format>
#include <utility>
#include <vector>

namespace application {

    std::optional<std::filesystem::path> pathOfUri(std::string_view uri) {
        constexpr std::string_view scheme = "file://";
        if (!uri.starts_with(scheme))
            return std::nullopt;
        uri.remove_prefix(scheme.size());
        std::string path;
        for (std::size_t index = 0; index < uri.size(); ++index) {
            if (uri[index] == '%' && index + 2 < uri.size()) {
                const auto hex = std::string{uri.substr(index + 1, 2)};
                path += static_cast<char>(std::stoi(hex, nullptr, 16));
                index += 2;
            } else {
                path += uri[index];
            }
        }
        if (path.empty() || path.front() != '/')
            return std::nullopt;
        return std::filesystem::path{path};
    }

    FileChooser::FileChooser(EventLoop loop) : loop{std::move(loop)} {}

    FileChooser::~FileChooser() {
        if (watched >= 0)
            loop.unwatch(watched);
        if (response != nullptr)
            sd_bus_slot_unref(response);
        if (bus != nullptr)
            sd_bus_flush_close_unref(bus);
    }

    std::expected<void, std::string> FileChooser::chooseImage(const std::string& title, std::function<void(std::optional<std::filesystem::path>)> done) {
        return open(title, false, std::move(done));
    }

    std::expected<void, std::string> FileChooser::chooseFolder(const std::string& title, std::function<void(std::optional<std::filesystem::path>)> done) {
        return open(title, true, std::move(done));
    }

    std::expected<void, std::string> FileChooser::open(const std::string& title, bool folder, std::function<void(std::optional<std::filesystem::path>)> done) {
        if (waiting)
            return std::unexpected{"the file chooser is already open"};
        if (bus == nullptr) {
            if (const int failed = sd_bus_open_user(&bus); failed < 0) {
                bus = nullptr;
                return std::unexpected{std::format("cannot reach the session bus: {}", std::strerror(-failed))}; // NOLINT(concurrency-mt-unsafe): the UI thread only
            }
            watched = sd_bus_get_fd(bus);
            loop.watch(watched, [this] {
                while (bus != nullptr && sd_bus_process(bus, nullptr) > 0) {
                }
            });
        }

        // The portal answers on a request object named after our bus name and a token we
        // choose, so the answer can be listened for before asking.
        const char* unique = nullptr;
        sd_bus_get_unique_name(bus, &unique);
        std::string sender = unique != nullptr ? unique + 1 : "";
        for (auto& character : sender)
            if (character == '.')
                character = '_';
        const auto token  = std::format("lantern_hyprland_{}", ++requests);
        const auto handle = std::format("/org/freedesktop/portal/desktop/request/{}/{}", sender, token);

        if (response != nullptr)
            response = sd_bus_slot_unref(response);
        if (const int failed = sd_bus_match_signal(bus, &response, "org.freedesktop.portal.Desktop", handle.c_str(), "org.freedesktop.portal.Request", "Response", answered, this);
            failed < 0)
            return std::unexpected{std::format("cannot listen for the file chooser: {}", std::strerror(-failed))}; // NOLINT(concurrency-mt-unsafe): the UI thread only

        sd_bus_message* call = nullptr;
        sd_bus_message_new_method_call(bus, &call, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.FileChooser", "OpenFile");
        // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg): sd-bus's C interface
        const int appended = folder ? sd_bus_message_append(call, "ssa{sv}", "", title.c_str(), 2, "handle_token", "s", token.c_str(), "directory", "b", 1)
                                    : sd_bus_message_append(call, "ssa{sv}", "", title.c_str(), 2, "handle_token", "s", token.c_str(), "filters", "a(sa(us))", 1, "Images", 5,
                                                            1U, "image/png", 1U, "image/jpeg", 1U, "image/webp", 1U, "image/jxl", 1U, "image/bmp");
        // NOLINTEND(cppcoreguidelines-pro-type-vararg)
        const int sent = appended < 0 ? appended : sd_bus_call_async(bus, nullptr, call, nullptr, nullptr, 0);
        sd_bus_message_unref(call);
        if (sent < 0)
            return std::unexpected{std::format("cannot open the file chooser: {}", std::strerror(-sent))}; // NOLINT(concurrency-mt-unsafe): the UI thread only
        sd_bus_flush(bus);
        waiting = std::move(done);
        return {};
    }

    int FileChooser::answered(sd_bus_message* message, void* self, sd_bus_error*) {
        auto&    chooser = *static_cast<FileChooser*>(self);
        uint32_t code    = 2;
        std::optional<std::filesystem::path> chosen;
        if (sd_bus_message_read(message, "u", &code) >= 0 && code == 0 && sd_bus_message_enter_container(message, 'a', "{sv}") > 0) { // NOLINT(cppcoreguidelines-pro-type-vararg): sd-bus
            while (sd_bus_message_enter_container(message, 'e', "sv") > 0) {
                const char* key = nullptr;
                sd_bus_message_read(message, "s", &key); // NOLINT(cppcoreguidelines-pro-type-vararg): sd-bus
                if (key != nullptr && std::string_view{key} == "uris" && sd_bus_message_enter_container(message, 'v', "as") > 0) {
                    char** uris = nullptr;
                    if (sd_bus_message_read_strv(message, &uris) >= 0 && uris != nullptr) {
                        if (uris[0] != nullptr) // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): a C string array
                            chosen = pathOfUri(uris[0]); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): a C string array
                        for (char** each = uris; *each != nullptr; ++each) // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): a C string array
                            std::free(*each); // NOLINT(cppcoreguidelines-no-malloc): sd-bus allocates with malloc
                        std::free(static_cast<void*>(uris)); // NOLINT(cppcoreguidelines-no-malloc): sd-bus allocates with malloc
                    }
                    sd_bus_message_exit_container(message);
                } else {
                    sd_bus_message_skip(message, "v");
                }
                sd_bus_message_exit_container(message);
            }
            sd_bus_message_exit_container(message);
        }
        chooser.finish(chosen);
        return 0;
    }

    void FileChooser::finish(std::optional<std::filesystem::path> chosen) {
        // Run after sd-bus is done with this message, not from inside its handler.
        auto done = std::exchange(waiting, {});
        if (done)
            loop.after(std::chrono::milliseconds{0}, [done = std::move(done), chosen = std::move(chosen)] { done(chosen); });
    }

}
