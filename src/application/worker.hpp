// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "foundation/file-descriptor.hpp"

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace application {

    // Runs blocking work off the UI thread. A job runs on the worker and returns a
    // continuation, which runs back on the UI thread when that thread calls
    // runFinished(). descriptor() becomes readable whenever continuations are waiting,
    // so the UI's event loop can watch it instead of anything waiting.
    class Worker {
      public:
        using Continuation = std::move_only_function<void()>;
        using Job          = std::move_only_function<Continuation()>;

        Worker();
        Worker(const Worker&)            = delete;
        Worker& operator=(const Worker&) = delete;

        [[nodiscard]] int descriptor() const { return ready.get(); }

        void              post(Job job);
        void              runFinished();

      private:
        void                        work(const std::stop_token& stop);

        foundation::FileDescriptor  ready;
        std::mutex                  mutex;
        std::condition_variable_any wake;
        std::deque<Job>             jobs;
        std::vector<Continuation>   finished;

        // Last, so it is stopped and joined before anything it uses is destroyed.
        std::jthread thread;
    };

}
