// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "foundation/file-descriptor.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include <sys/types.h>

namespace application {

    // An outside program the user turns on and off from Lantern, such as Waybar. Off
    // stops every running copy, whoever started it, because the user asked for exactly
    // that; on starts it the way Hyprland would. Neither changes what starts at login.
    class OutsideProgram {
      public:
        // `changed` runs whenever running(), busy() or error() may have changed.
        OutsideProgram(std::filesystem::path program, EventLoop loop, std::function<void()> changed);
        ~OutsideProgram();
        OutsideProgram(const OutsideProgram&)            = delete;
        OutsideProgram& operator=(const OutsideProgram&) = delete;

        [[nodiscard]] bool                              running() const { return !watched.empty(); }
        // Stopping or starting, and not yet seen to be done.
        [[nodiscard]] bool                              busy() const { return stopping || starting; }
        [[nodiscard]] const std::optional<std::string>& error() const { return lastError; }

        // Looks again at what is running, for when something else started or stopped it.
        void                                            refresh();
        void                                            turnOn();
        void                                            turnOff();
        // Stops it and starts it again, so it reads its config anew. Does nothing when it is
        // off: then the user has chosen not to run it.
        void                                            restart();

      private:
        void                                            watch(pid_t process);
        void                                            exited(pid_t process);
        void                                            start();
        void                                            stopAll();

        std::filesystem::path                           program;
        std::string                                     name;
        EventLoop                                       loop;
        std::function<void()>                           changed;
        std::map<pid_t, foundation::FileDescriptor>     watched;
        bool                                            stopping     = false;
        bool                                            starting     = false;
        bool                                            startAfterStop = false;
        std::optional<std::string>                      lastError;
        // Shared with the timers, which may fire after this is gone.
        std::shared_ptr<bool>                           alive = std::make_shared<bool>(true);
    };

}
