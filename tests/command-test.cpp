// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/command.hpp"
#include "application/file-chooser.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <map>

#include <poll.h>

namespace {

    // An event loop of Lantern's shape over poll, run until nothing is watched.
    struct PollLoop {
        std::map<int, std::function<void()>> watched;

        application::EventLoop loop() {
            return {
                .watch   = [this](int descriptor, std::function<void()> readable) { watched[descriptor] = std::move(readable); },
                .unwatch = [this](int descriptor) { watched.erase(descriptor); },
                .after   = [](std::chrono::milliseconds, std::function<void()>) {},
            };
        }

        void run() {
            for (int round = 0; round < 100 && !watched.empty(); ++round) {
                std::vector<pollfd> descriptors;
                for (const auto& [descriptor, readable] : watched)
                    descriptors.push_back({descriptor, POLLIN, 0});
                if (::poll(descriptors.data(), descriptors.size(), 100) <= 0)
                    continue;
                for (const auto& ready : descriptors)
                    if ((ready.revents & POLLIN) != 0 && watched.contains(ready.fd))
                        watched.at(ready.fd)();
            }
        }
    };

    TEST(Command, ReportsWhatItPrintedAndWhetherItSucceeded) {
        PollLoop    polling;
        bool        succeeded = false;
        std::string output;
        ASSERT_TRUE(application::runCommand(polling.loop(), "/bin/sh", {"-c", "echo hello; echo there >&2"}, [&](bool ok, std::string printed) {
            succeeded = ok;
            output    = std::move(printed);
        }));
        polling.run();
        EXPECT_TRUE(succeeded);
        EXPECT_EQ(output, "hello\nthere\n");

        bool failed = true;
        ASSERT_TRUE(application::runCommand(polling.loop(), "/bin/sh", {"-c", "exit 3"}, [&](bool ok, std::string) { failed = !ok; }));
        polling.run();
        EXPECT_TRUE(failed);
    }

    TEST(FileChooser, ReadsThePathInAFileUri) {
        EXPECT_EQ(application::pathOfUri("file:///home/me/My%20Pictures/sea%2B.png"), std::filesystem::path{"/home/me/My Pictures/sea+.png"});
        EXPECT_FALSE(application::pathOfUri("https://example.org/a.png").has_value());
    }

}
