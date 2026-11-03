// SPDX-License-Identifier: GPL-3.0-or-later
// lantern-paper, Lantern's own wallpaper program. It draws the wallpaper on Hyprland's
// background layer, keeps each workspace's image drawn and ready, and listens to
// Hyprland's events, so a workspace's wallpaper shows the moment the workspace does.
// It reads wallpaper.json in Lantern's folder, and in $XDG_RUNTIME_DIR the random choices
// made since it started and an image being tried.

#include "desktop/wallpaper.hpp"
#include "foundation/environment.hpp"
#include "ipc/events.hpp"
#include "ipc/instance.hpp"
#include "ipc/request.hpp"
#include "ipc/tree.hpp"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <cairo/cairo.h>
#include <hyprgraphics/image/Image.hpp>
#include <wayland-client.h>

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <print>
#include <random>
#include <set>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace {

    using Choices = std::map<int, std::filesystem::path>;

    std::optional<std::string> readText(const std::filesystem::path& file) {
        std::ifstream stream{file};
        if (!stream)
            return std::nullopt;
        std::stringstream text;
        text << stream.rdbuf();
        return text.str();
    }

    // A wallpaper drawn at a size in pixels.
    struct Key {
        std::filesystem::path image;
        desktop::FitMode      fit    = desktop::FitMode::cover;
        int                   width  = 0;
        int                   height = 0;

        auto operator<=>(const Key&) const = default;
    };

    struct Drawn {
        Key         key;
        int         memory = -1;
        int         stride = 0;
        std::string error;
    };

    Drawn paint(const Key& key) {
        Drawn                drawn{.key = key, .memory = -1, .stride = 0, .error = {}};
        Hyprgraphics::CImage image{key.image.string()};
        if (!image.success()) {
            drawn.error = image.getError();
            return drawn;
        }
        cairo_surface_t* source       = image.cairoSurface()->cairo();
        const double     sourceWidth  = cairo_image_surface_get_width(source);
        const double     sourceHeight = cairo_image_surface_get_height(source);
        const int        stride       = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, key.width);
        const auto       size         = static_cast<std::size_t>(stride) * static_cast<std::size_t>(key.height);
        const int        memory       = ::memfd_create("lantern-paper", MFD_CLOEXEC);
        if (memory < 0 || ::ftruncate(memory, static_cast<off_t>(size)) != 0 || sourceWidth <= 0 || sourceHeight <= 0) {
            drawn.error = "cannot make room for it";
            if (memory >= 0)
                ::close(memory);
            return drawn;
        }
        void* pixels = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, memory, 0);
        if (pixels == MAP_FAILED) {
            drawn.error = "cannot make room for it";
            ::close(memory);
            return drawn;
        }
        cairo_surface_t* target = cairo_image_surface_create_for_data(static_cast<unsigned char*>(pixels), CAIRO_FORMAT_ARGB32, key.width, key.height, stride);
        cairo_t*         cairo  = cairo_create(target);
        cairo_set_source_rgb(cairo, 0, 0, 0);
        cairo_paint(cairo);
        const double width  = key.width;
        const double height = key.height;
        if (key.fit == desktop::FitMode::tile) {
            cairo_pattern_t* pattern = cairo_pattern_create_for_surface(source);
            cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
            cairo_set_source(cairo, pattern);
            cairo_paint(cairo);
            cairo_pattern_destroy(pattern);
        } else {
            double across = width / sourceWidth;
            double down   = height / sourceHeight;
            if (key.fit == desktop::FitMode::cover)
                across = down = std::max(across, down);
            else if (key.fit == desktop::FitMode::contain)
                across = down = std::min(across, down);
            cairo_translate(cairo, (width - sourceWidth * across) / 2, (height - sourceHeight * down) / 2);
            cairo_scale(cairo, across, down);
            cairo_set_source_surface(cairo, source, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cairo), CAIRO_FILTER_GOOD);
            cairo_paint(cairo);
        }
        cairo_destroy(cairo);
        cairo_surface_flush(target);
        cairo_surface_destroy(target);
        ::munmap(pixels, size);
        drawn.memory = memory;
        drawn.stride = stride;
        return drawn;
    }

    // Draws on a thread of its own, so the loop stays free to switch wallpapers while a
    // large image is decoded.
    class Painter {
      public:
        Painter() : wake{::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)}, thread{[this](std::stop_token stop) { run(std::move(stop)); }} {}
        ~Painter() {
            thread.request_stop();
            thread.join();
            ::close(wake);
        }
        Painter(const Painter&)            = delete;
        Painter& operator=(const Painter&) = delete;

        [[nodiscard]] int descriptor() const { return wake; }

        void ask(const Key& key) {
            const std::scoped_lock lock{mutex};
            if (!asked.insert(key).second)
                return;
            jobs.push_back(key);
            ready.notify_one();
        }

        [[nodiscard]] std::vector<Drawn> take() {
            std::uint64_t count = 0;
            (void)!::read(wake, &count, sizeof count);
            const std::scoped_lock lock{mutex};
            auto                   taken = std::move(done);
            done.clear();
            for (const auto& drawn : taken)
                asked.erase(drawn.key);
            return taken;
        }

      private:
        void run(std::stop_token stop) {
            while (true) {
                Key key;
                {
                    std::unique_lock lock{mutex};
                    if (!ready.wait(lock, stop, [this] { return !jobs.empty(); }))
                        return;
                    key = jobs.front();
                    jobs.pop_front();
                }
                auto drawn = paint(key);
                {
                    const std::scoped_lock lock{mutex};
                    done.push_back(std::move(drawn));
                }
                const std::uint64_t one = 1;
                (void)!::write(wake, &one, sizeof one);
            }
        }

        std::mutex                  mutex;
        std::condition_variable_any ready;
        std::deque<Key>             jobs;
        std::set<Key>               asked;
        std::vector<Drawn>          done;
        int                         wake;
        std::jthread                thread;
    };

    class Paper;

    struct Output {
        Paper*                 paper   = nullptr;
        std::uint32_t          global  = 0;
        wl_output*             output  = nullptr;
        std::string            name;
        int                    scale   = 1;
        wl_surface*            surface = nullptr;
        zwlr_layer_surface_v1* layer   = nullptr;
        int                    width   = 0;
        int                    height  = 0;
        std::optional<Key>     shown;
    };

    class Paper {
      public:
        explicit Paper(ipc::Instance instance) : environment{foundation::currentEnvironment()}, instance{std::move(instance)} {}

        int run() {
            display = wl_display_connect(nullptr);
            if (display == nullptr) {
                std::println(stderr, "lantern-paper: cannot reach the Wayland display");
                return 1;
            }
            registry = wl_display_get_registry(display);
            wl_registry_add_listener(registry, &registryListener, this);
            wl_display_roundtrip(display);
            if (compositor == nullptr || shm == nullptr || layerShell == nullptr) {
                std::println(stderr, "lantern-paper: the compositor has no layer shell");
                return 1;
            }
            wl_display_roundtrip(display);

            auto connection = ipc::EventConnection::open(instance);
            if (!connection) {
                std::println(stderr, "lantern-paper: {}", connection.error());
                return 1;
            }
            events.emplace(std::move(*connection));
            watchFiles();
            readSetup();
            // A new random set each time it starts, as at login, rather than the last one's.
            saveChoices();
            readTry();
            askHyprland(true);
            show();

            while (true) {
                while (wl_display_prepare_read(display) != 0)
                    wl_display_dispatch_pending(display);
                wl_display_flush(display);
                std::array<pollfd, 4> watched{pollfd{wl_display_get_fd(display), POLLIN, 0}, pollfd{events->descriptor(), POLLIN, 0}, pollfd{inotify, POLLIN, 0},
                                              pollfd{painter.descriptor(), POLLIN, 0}};
                if (::poll(watched.data(), watched.size(), -1) < 0) {
                    wl_display_cancel_read(display);
                    if (errno == EINTR)
                        continue;
                    return 1;
                }
                if ((watched[0].revents & POLLIN) != 0) {
                    if (wl_display_read_events(display) != 0)
                        return 1;
                } else {
                    wl_display_cancel_read(display);
                }
                if (wl_display_dispatch_pending(display) < 0)
                    return 1;
                if ((watched[0].revents & (POLLHUP | POLLERR)) != 0)
                    return 0;
                if ((watched[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0 && !hyprlandChanged())
                    return 0;
                if ((watched[2].revents & POLLIN) != 0)
                    filesChanged();
                if ((watched[3].revents & POLLIN) != 0)
                    painted();
            }
        }

        void show() {
            for (const auto& output : outputs) {
                if (output->layer == nullptr || output->width == 0)
                    continue;
                const auto wallpaper = wanted(*output);
                if (!wallpaper)
                    continue;
                const auto key   = keyOf(*output, *wallpaper);
                const auto found = buffers.find(key);
                if (found == buffers.end()) {
                    if (!failed.contains(key))
                        painter.ask(key);
                    continue;
                }
                if (output->shown == key)
                    continue;
                wl_surface_set_buffer_scale(output->surface, output->scale);
                wl_surface_attach(output->surface, found->second, 0, 0);
                wl_surface_damage_buffer(output->surface, 0, 0, INT32_MAX, INT32_MAX);
                wl_surface_commit(output->surface);
                output->shown = key;
            }
            prepare();
        }

        void configured(Output& output, std::uint32_t serial, std::uint32_t width, std::uint32_t height) {
            zwlr_layer_surface_v1_ack_configure(output.layer, serial);
            output.width  = static_cast<int>(width);
            output.height = static_cast<int>(height);
            output.shown.reset();
            show();
        }

        void closed(Output& output) {
            forget(output);
        }

        void described(Output& output) {
            if (output.layer == nullptr && !output.name.empty())
                cover(output);
            else
                show();
        }

      private:
        static void global(void* data, wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t version) {
            auto&            paper = *static_cast<Paper*>(data);
            std::string_view kind{interface};
            if (kind == wl_compositor_interface.name)
                paper.compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4U)));
            else if (kind == wl_shm_interface.name)
                paper.shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
            else if (kind == zwlr_layer_shell_v1_interface.name)
                paper.layerShell = static_cast<zwlr_layer_shell_v1*>(wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, std::min(version, 4U)));
            else if (kind == wl_output_interface.name && version >= 4) {
                auto output    = std::make_unique<Output>();
                output->paper  = &paper;
                output->global = name;
                output->output = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, 4));
                wl_output_add_listener(output->output, &outputListener, output.get());
                paper.outputs.push_back(std::move(output));
            }
        }

        static void globalRemoved(void* data, wl_registry*, std::uint32_t name) {
            auto&      paper = *static_cast<Paper*>(data);
            const auto found = std::ranges::find_if(paper.outputs, [name](const auto& output) { return output->global == name; });
            if (found == paper.outputs.end())
                return;
            paper.forget(**found);
            wl_output_release((*found)->output);
            paper.outputs.erase(found);
        }

        static constexpr wl_registry_listener registryListener{.global = global, .global_remove = globalRemoved};

        static constexpr wl_output_listener outputListener{
            .geometry    = [](void*, wl_output*, std::int32_t, std::int32_t, std::int32_t, std::int32_t, std::int32_t, const char*, const char*, std::int32_t) {},
            .mode        = [](void*, wl_output*, std::uint32_t, std::int32_t, std::int32_t, std::int32_t) {},
            .done        = [](void* data, wl_output*) { static_cast<Output*>(data)->paper->described(*static_cast<Output*>(data)); },
            .scale       = [](void* data, wl_output*, std::int32_t factor) { static_cast<Output*>(data)->scale = std::max(1, factor); },
            .name        = [](void* data, wl_output*, const char* name) { static_cast<Output*>(data)->name = name; },
            .description = [](void*, wl_output*, const char*) {},
        };

        static constexpr zwlr_layer_surface_v1_listener layerListener{
            .configure = [](void* data, zwlr_layer_surface_v1*, std::uint32_t serial, std::uint32_t width,
                            std::uint32_t height) { static_cast<Output*>(data)->paper->configured(*static_cast<Output*>(data), serial, width, height); },
            .closed    = [](void* data, zwlr_layer_surface_v1*) { static_cast<Output*>(data)->paper->closed(*static_cast<Output*>(data)); },
        };

        // Puts a surface for the wallpaper on the output's background layer, under everything.
        void cover(Output& output) {
            output.surface     = wl_compositor_create_surface(compositor);
            wl_region* nowhere = wl_compositor_create_region(compositor);
            wl_surface_set_input_region(output.surface, nowhere);
            wl_region_destroy(nowhere);
            output.layer = zwlr_layer_shell_v1_get_layer_surface(layerShell, output.surface, output.output, ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "wallpaper");
            zwlr_layer_surface_v1_add_listener(output.layer, &layerListener, &output);
            zwlr_layer_surface_v1_set_anchor(output.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                                               ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
            zwlr_layer_surface_v1_set_exclusive_zone(output.layer, -1);
            zwlr_layer_surface_v1_set_size(output.layer, 0, 0);
            wl_surface_commit(output.surface);
        }

        void forget(Output& output) {
            if (output.layer != nullptr)
                zwlr_layer_surface_v1_destroy(output.layer);
            if (output.surface != nullptr)
                wl_surface_destroy(output.surface);
            output.layer   = nullptr;
            output.surface = nullptr;
            output.width   = 0;
            output.shown.reset();
        }

        void watchFiles() {
            inotify = ::inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
            std::error_code error;
            std::filesystem::create_directories(desktop::wallpaperSetupFile(environment).parent_path(), error);
            for (const auto& folder : {desktop::wallpaperSetupFile(environment).parent_path(), desktop::randomChoicesFile(environment).parent_path()})
                ::inotify_add_watch(inotify, folder.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE | IN_MOVED_FROM);
        }

        void filesChanged() {
            alignas(inotify_event) std::array<char, 4096> chunk{};
            bool setupChanged = false, choicesChanged = false, tryChanged = false;
            while (true) {
                const auto length = ::read(inotify, chunk.data(), chunk.size());
                if (length <= 0)
                    break;
                for (std::size_t at = 0; at < static_cast<std::size_t>(length);) {
                    const auto*            event = reinterpret_cast<const inotify_event*>(chunk.data() + at); // NOLINT: inotify's layout
                    const std::string_view name  = event->len > 0 ? std::string_view{event->name} : std::string_view{};
                    setupChanged |= name == desktop::wallpaperSetupFile(environment).filename().string();
                    choicesChanged |= name == desktop::randomChoicesFile(environment).filename().string();
                    tryChanged |= name == desktop::tryWallpaperFile(environment).filename().string();
                    at += sizeof(inotify_event) + event->len;
                }
            }
            if (setupChanged)
                readSetup();
            if (choicesChanged)
                readChoices();
            if (tryChanged)
                readTry();
            if (setupChanged || choicesChanged || tryChanged)
                show();
        }

        void readSetup() {
            const auto text   = readText(desktop::wallpaperSetupFile(environment));
            auto       read   = text ? desktop::wallpaperSetupIn(*text) : std::nullopt;
            const auto before = setup ? setup->perWorkspace.randomFrom : std::nullopt;
            setup             = std::move(read);
            failed.clear();
            nextPick.reset();
            // Choices from another folder are not this one's.
            if (setup && setup->perWorkspace.randomFrom != before && before) {
                choices.clear();
                saveChoices();
            }
        }

        void readChoices() {
            const auto text = readText(desktop::randomChoicesFile(environment));
            choices         = text ? desktop::randomChoicesIn(*text) : Choices{};
            nextPick.reset();
        }

        void readTry() {
            const auto text = readText(desktop::tryWallpaperFile(environment));
            const auto read = text ? desktop::wallpaperSetupIn(*text) : std::nullopt;
            trying          = read ? std::optional{read->main} : std::nullopt;
        }

        void saveChoices() {
            std::ofstream{desktop::randomChoicesFile(environment)} << desktop::randomChoicesText(choices);
        }

        // Asks Hyprland which workspace each monitor shows, and with `all`, which exist.
        void askHyprland(bool all) {
            const auto monitorReply = ipc::request(instance, ipc::Query::monitors);
            if (!monitorReply)
                return;
            std::string workspaceReply = "[]";
            if (all)
                if (const auto reply = ipc::request(instance, ipc::Query::workspaces))
                    workspaceReply = *reply;
            const auto tree = ipc::parseTree(*monitorReply, workspaceReply, "[]");
            if (!tree)
                return;
            monitors = tree->monitors;
            if (all) {
                workspaces.clear();
                for (const auto& workspace : tree->workspaces)
                    if (workspace.id > 0)
                        workspaces.push_back(workspace.id);
            }
        }

        bool hyprlandChanged() {
            const auto drained = events->drain();
            if (!drained)
                return false;
            bool switched = false, changed = false;
            for (const auto& event : *drained) {
                switched |= event.name == "workspacev2" || event.name == "focusedmonv2";
                changed |= event.name == "createworkspacev2" || event.name == "destroyworkspacev2" || event.name == "moveworkspacev2" || event.name == "monitoraddedv2" ||
                    event.name == "monitorremovedv2";
            }
            if (switched || changed) {
                askHyprland(changed);
                show();
            }
            return true;
        }

        void painted() {
            for (auto& drawn : painter.take()) {
                if (drawn.memory < 0) {
                    std::println(stderr, "lantern-paper: {} was not shown: {}", drawn.key.image.string(), drawn.error);
                    failed.insert(drawn.key);
                    continue;
                }
                wl_shm_pool* pool = wl_shm_create_pool(shm, drawn.memory, drawn.stride * drawn.key.height);
                buffers[drawn.key] = wl_shm_pool_create_buffer(pool, 0, drawn.key.width, drawn.key.height, drawn.stride, WL_SHM_FORMAT_XRGB8888);
                wl_shm_pool_destroy(pool);
                ::close(drawn.memory);
            }
            show();
        }

        // What the output should show: the image being tried, or its workspace's wallpaper.
        // A workspace without its random image yet is given one now.
        std::optional<desktop::Wallpaper> wanted(const Output& output) {
            if (trying)
                return trying;
            if (!setup)
                return std::nullopt;
            const auto monitor = std::ranges::find(monitors, output.name, &ipc::Monitor::name);
            if (monitor == monitors.end())
                return setup->main;
            const int number = monitor->activeWorkspace;
            if (setup->perWorkspace.randomFrom && !choices.contains(number)) {
                const auto image = nextPick ? nextPick : pick();
                if (!image)
                    return std::nullopt;
                choices[number] = *image;
                nextPick.reset();
                saveChoices();
            }
            return desktop::wallpaperOfWorkspace(*setup, number, choices);
        }

        std::optional<std::filesystem::path> pick() {
            if (!setup || !setup->perWorkspace.randomFrom)
                return std::nullopt;
            const auto images = desktop::randomCandidates(*setup->perWorkspace.randomFrom);
            return desktop::randomPick(choices, images, std::random_device{}());
        }

        Key keyOf(const Output& output, const desktop::Wallpaper& wallpaper) const {
            return Key{wallpaper.image, wallpaper.fit, output.width * output.scale, output.height * output.scale};
        }

        // Draws ahead what a switch may need: every workspace's wallpaper, and the next random
        // image for a new workspace. Lets go of the rest.
        void prepare() {
            if (setup && setup->perWorkspace.randomFrom && !nextPick)
                nextPick = pick();
            std::set<Key> needed;
            for (const auto& output : outputs) {
                if (output->layer == nullptr || output->width == 0)
                    continue;
                if (output->shown)
                    needed.insert(*output->shown);
                if (trying)
                    needed.insert(keyOf(*output, *trying));
                if (!setup)
                    continue;
                if (setup->perWorkspace.randomFrom) {
                    for (const auto number : workspaces)
                        if (const auto chosen = choices.find(number); chosen != choices.end())
                            needed.insert(keyOf(*output, {chosen->second, setup->main.fit}));
                    if (nextPick)
                        needed.insert(keyOf(*output, {*nextPick, setup->main.fit}));
                } else {
                    needed.insert(keyOf(*output, setup->main));
                    for (const auto& [number, wallpaper] : setup->perWorkspace.own)
                        needed.insert(keyOf(*output, wallpaper));
                }
            }
            for (const auto& key : needed)
                if (!buffers.contains(key) && !failed.contains(key))
                    painter.ask(key);
            for (auto at = buffers.begin(); at != buffers.end();) {
                if (needed.contains(at->first)) {
                    ++at;
                    continue;
                }
                wl_buffer_destroy(at->second);
                at = buffers.erase(at);
            }
        }

        foundation::Environment              environment;
        ipc::Instance                        instance;
        wl_display*                          display    = nullptr;
        wl_registry*                         registry   = nullptr;
        wl_compositor*                       compositor = nullptr;
        wl_shm*                              shm        = nullptr;
        zwlr_layer_shell_v1*                 layerShell = nullptr;
        std::vector<std::unique_ptr<Output>> outputs;
        std::optional<ipc::EventConnection>  events;
        int                                  inotify = -1;
        std::optional<desktop::WallpaperSetup> setup;
        Choices                              choices;
        std::optional<desktop::Wallpaper>    trying;
        std::optional<std::filesystem::path> nextPick;
        std::vector<ipc::Monitor>            monitors;
        std::vector<int>                     workspaces;
        std::map<Key, wl_buffer*>            buffers;
        std::set<Key>                        failed;
        Painter                              painter;
    };

}

int main() {
    const auto instance = ipc::currentInstance();
    if (!instance) {
        std::println(stderr, "lantern-paper: {}", instance.error());
        return 1;
    }
    Paper paper{*instance};
    return paper.run();
}
