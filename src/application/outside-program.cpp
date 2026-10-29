// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/outside-program.hpp"

#include "desktop/process.hpp"

#include <chrono>
#include <format>

namespace application {

    namespace {

        using namespace std::chrono_literals;

        // How long a started program has to show up before Lantern says it did not.
        constexpr auto startTimeout = 3s;
        constexpr auto lookEvery    = 200ms;

    }

    OutsideProgram::OutsideProgram(std::filesystem::path program, EventLoop loop, std::function<void()> changed) :
        program{std::move(program)}, name{this->program.filename().string()}, loop{std::move(loop)}, changed{std::move(changed)} {
        refresh();
    }

    OutsideProgram::~OutsideProgram() {
        *alive = false;
        for (const auto& [process, exit] : watched)
            loop.unwatch(exit.get());
    }

    void OutsideProgram::refresh() {
        for (const auto process : desktop::findProcesses(name))
            if (!watched.contains(process))
                watch(process);
        changed();
    }

    void OutsideProgram::watch(pid_t process) {
        auto exit = desktop::exitOf(process);
        if (!exit)
            return; // it has already gone
        const int descriptor = exit->get();
        watched.emplace(process, std::move(*exit));
        loop.watch(descriptor, [this, process] { exited(process); });
    }

    void OutsideProgram::exited(pid_t process) {
        if (const auto found = watched.find(process); found != watched.end()) {
            loop.unwatch(found->second.get());
            watched.erase(found);
        }
        if (watched.empty() && stopping) {
            stopping = false;
            if (startAfterStop) {
                startAfterStop = false;
                start();
            }
        }
        changed();
    }

    void OutsideProgram::turnOn() {
        lastError.reset();
        startAfterStop = false;
        if (running() || starting)
            return changed();
        start();
        changed();
    }

    void OutsideProgram::turnOff() {
        lastError.reset();
        startAfterStop = false;
        stopAll();
        changed();
    }

    void OutsideProgram::restart() {
        if (!running() || stopping)
            return;
        lastError.reset();
        startAfterStop = true;
        stopAll();
        changed();
    }

    void OutsideProgram::stopAll() {
        if (watched.empty())
            return;
        stopping = true;
        for (const auto& [process, exit] : watched) {
            if (const auto stopped = desktop::stopProcess(process); !stopped)
                lastError = stopped.error();
        }
    }

    void OutsideProgram::start() {
        if (const auto started = desktop::startDetached(program); !started) {
            lastError = started.error();
            return;
        }
        // The program is not Lantern's child, so Lantern learns of it by looking.
        starting                     = true;
        const auto deadline          = std::chrono::steady_clock::now() + startTimeout;
        auto       look              = std::make_shared<std::function<void()>>();
        *look                        = [this, deadline, weak = std::weak_ptr<std::function<void()>>{look}, alive = alive] {
            if (!*alive)
                return;
            for (const auto process : desktop::findProcesses(name))
                if (!watched.contains(process))
                    watch(process);
            if (running() || std::chrono::steady_clock::now() > deadline) {
                starting = false;
                if (!running())
                    lastError = std::format("{} was started but is not running; it may have stopped at once", name);
                changed();
                return;
            }
            if (const auto again = weak.lock())
                loop.after(std::chrono::duration_cast<std::chrono::milliseconds>(lookEvery), [again] { (*again)(); });
        };
        loop.after(std::chrono::duration_cast<std::chrono::milliseconds>(lookEvery), [look] { (*look)(); });
    }

}
