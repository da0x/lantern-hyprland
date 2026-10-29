// SPDX-License-Identifier: GPL-3.0-or-later
#include "ipc/request.hpp"

#include "ipc/socket.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <sstream>
#include <string_view>

#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace ipc {

    namespace {

        std::string_view command(Query query) {
            switch (query) {
                case Query::bindings: return "j/binds";
                case Query::clients: return "j/clients";
                case Query::descriptions: return "j/descriptions";
                case Query::monitors: return "j/monitors";
                case Query::workspaces: return "j/workspaces";
                case Query::status: return "j/status";
            }
            return {};
        }

        constexpr std::array consoleList{
            ConsoleRequest{"activewindow", "the window with focus"},
            ConsoleRequest{"activeworkspace", "the workspace on the focused monitor"},
            ConsoleRequest{"animations", "every animation and its curve"},
            ConsoleRequest{"binds", "every keyboard shortcut"},
            ConsoleRequest{"clients", "every window"},
            ConsoleRequest{"configerrors", "what is wrong in the config, if anything"},
            ConsoleRequest{"cursorpos", "where the pointer is"},
            ConsoleRequest{"descriptions", "every option, what it does and its default"},
            ConsoleRequest{"devices", "keyboards, mice, tablets and switches"},
            ConsoleRequest{"getoption", "one option: getoption general:gaps_in"},
            ConsoleRequest{"globalshortcuts", "shortcuts apps have asked for"},
            ConsoleRequest{"layers", "bars, notifications and other layers"},
            ConsoleRequest{"locked", "whether the session is locked"},
            ConsoleRequest{"monitors", "monitors in use; add all for the rest"},
            ConsoleRequest{"rollinglog", "Hyprland's latest log lines"},
            ConsoleRequest{"splash", "the splash line"},
            ConsoleRequest{"status", "how the config is read"},
            ConsoleRequest{"systeminfo", "version, system and graphics cards"},
            ConsoleRequest{"version", "Hyprland's version and how it was built"},
            ConsoleRequest{"workspacerules", "workspace rules"},
            ConsoleRequest{"workspaces", "every workspace"},
        };

        // Requests that change Hyprland, refused by name so the reason can say so.
        constexpr std::array<std::string_view, 15> changing{
            "dispatch", "keyword", "reload", "kill", "setcursor", "output", "switchxkblayout", "seterror",
            "setprop", "notify", "dismissnotify", "plugin", "eval", "hyprpaper", "hyprsunset",
        };

        bool isOptionName(std::string_view name) {
            return !name.empty() && std::ranges::all_of(name, [](char c) { return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || std::string_view{"_.:-"}.contains(c); });
        }

        // Sends `text` and waits for the whole reply. `label` names it in errors.
        std::expected<std::string, std::string> send(const Instance& instance, std::string_view text, std::string_view label) {
            auto socket = connectTo(instance.requestSocket);
            if (!socket)
                return std::unexpected{socket.error()};

            // Hyprland gives up on a request after 5 seconds, so waiting longer gains nothing.
            const timeval timeout{.tv_sec = 5, .tv_usec = 0};
            ::setsockopt(socket->get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            ::setsockopt(socket->get(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

            if (::write(socket->get(), text.data(), text.size()) != static_cast<ssize_t>(text.size()))
                return std::unexpected{std::format("cannot send \"{}\": {}", label, lastError())};

            std::string            reply;
            std::array<char, 8192> buffer{};
            while (true) {
                const auto count = ::read(socket->get(), buffer.data(), buffer.size());
                if (count == 0)
                    return reply;
                if (count < 0)
                    return std::unexpected{std::format("no reply to \"{}\": {}", label, lastError())};
                reply.append(buffer.data(), static_cast<std::size_t>(count));
            }
        }

    }

    std::expected<std::string, std::string> request(const Instance& instance, Query query) {
        return send(instance, command(query), command(query));
    }

    std::expected<std::string, std::string> requestOptions(const Instance& instance, const std::vector<std::string>& names) {
        std::string batch = "[[BATCH]]";
        for (const auto& name : names) {
            if (!isOptionName(name))
                return std::unexpected{std::format("\"{}\" is not an option's name", name)};
            batch += std::format("{}j/getoption {}", batch.size() > 9 ? ";" : "", name);
        }
        return send(instance, batch, "j/getoption");
    }

    std::span<const ConsoleRequest> consoleRequests() {
        return consoleList;
    }

    std::expected<std::string, std::string> consoleCommand(std::string_view typed) {
        std::vector<std::string> words;
        std::istringstream       split{std::string{typed}};
        for (std::string word; split >> word;)
            if (word != "-j" && word != "--json")
                words.push_back(word);
        if (!words.empty() && words.front() == "hyprctl")
            words.erase(words.begin());
        if (!words.empty() && words.front().starts_with("j/"))
            words.front().erase(0, 2);
        if (words.empty())
            return std::unexpected{std::string{"Type a request, such as monitors."}};

        const auto& name = words.front();
        if (std::ranges::contains(changing, name))
            return std::unexpected{std::format("{} changes Hyprland, and the console only sends requests that read.", name)};
        if (name == "getoption") {
            if (words.size() != 2)
                return std::unexpected{std::string{"getoption takes one option's name, such as getoption general:gaps_in."}};
            if (!isOptionName(words[1]))
                return std::unexpected{std::format("\"{}\" is not an option's name.", words[1])};
            return std::format("j/getoption {}", words[1]);
        }
        if (!std::ranges::contains(consoleList, name, &ConsoleRequest::name))
            return std::unexpected{std::format("The console does not send \"{}\". It sends only the requests listed here, which read.", name)};
        if (name == "monitors" && words.size() == 2 && words[1] == "all")
            return std::string{"j/monitors all"};
        if (words.size() > 1)
            return std::unexpected{std::format("{} takes nothing after it.", name)};
        return "j/" + name;
    }

    std::expected<std::string, std::string> requestTyped(const Instance& instance, std::string_view typed) {
        const auto command = consoleCommand(typed);
        if (!command)
            return command;
        return send(instance, *command, *command);
    }

    std::optional<std::string> indentedJson(std::string_view reply) {
        const auto parsed = nlohmann::ordered_json::parse(reply, nullptr, false);
        if (parsed.is_discarded())
            return std::nullopt;
        return parsed.dump(2, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
    }

}
