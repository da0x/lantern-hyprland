// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/worker.hpp"

#include <cstdint>
#include <stdexcept>

#include <sys/eventfd.h>
#include <unistd.h>

namespace application {

    Worker::Worker() : ready{::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)} {
        if (!ready.valid())
            throw std::runtime_error{"cannot create the worker's eventfd"};
        thread = std::jthread{[this](const std::stop_token& stop) { work(stop); }};
    }

    void Worker::post(Job job) {
        {
            const std::scoped_lock lock{mutex};
            jobs.push_back(std::move(job));
        }
        wake.notify_one();
    }

    void Worker::runFinished() {
        std::uint64_t count = 0;
        static_cast<void>(::read(ready.get(), &count, sizeof(count)));

        std::vector<Continuation> taken;
        {
            const std::scoped_lock lock{mutex};
            taken.swap(finished);
        }
        for (auto& continuation : taken)
            continuation();
    }

    void Worker::work(const std::stop_token& stop) {
        while (true) {
            Job job;
            {
                std::unique_lock lock{mutex};
                if (!wake.wait(lock, stop, [this] { return !jobs.empty(); }))
                    return;
                job = std::move(jobs.front());
                jobs.pop_front();
            }

            auto continuation = job();
            {
                const std::scoped_lock lock{mutex};
                finished.push_back(std::move(continuation));
            }
            const std::uint64_t one = 1;
            static_cast<void>(::write(ready.get(), &one, sizeof(one)));
        }
    }

}
