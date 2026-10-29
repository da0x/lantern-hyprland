// SPDX-License-Identifier: GPL-3.0-or-later
#include "ipc/events.hpp"

#include <gtest/gtest.h>

using ipc::Event;

TEST(EventSplitter, SplitsWholeLines) {
    ipc::EventSplitter splitter;
    EXPECT_EQ(splitter.feed("workspacev2>>2,2\nconfigreloaded>>\n"), (std::vector<Event>{{"workspacev2", "2,2"}, {"configreloaded", ""}}));
}

TEST(EventSplitter, KeepsALineCutInTwoUntilTheRestArrives) {
    ipc::EventSplitter splitter;
    EXPECT_TRUE(splitter.feed("openwindow>>5a1,1,kitty,a title, with").empty());
    EXPECT_EQ(splitter.feed(" a comma\nactivewindowv2>>5a1\n"),
              (std::vector<Event>{{"openwindow", "5a1,1,kitty,a title, with a comma"}, {"activewindowv2", "5a1"}}));
}

TEST(EventSplitter, SkipsLinesWithoutASeparator) {
    ipc::EventSplitter splitter;
    EXPECT_EQ(splitter.feed("garbage\nbell>>\n"), (std::vector<Event>{{"bell", ""}}));
}

#include "application/event-description.hpp"

TEST(EventDescription, SaysWhatAnEventMeansAndNamesWindowsItKnows) {
    const auto named = [](std::string_view address) -> std::optional<std::string> {
        if (address == "0x55ce")
            return "kitty";
        return std::nullopt;
    };
    const auto say = [&](std::string name, std::string data) { return application::describeEvent(ipc::Event{std::move(name), std::move(data)}, named); };
    EXPECT_EQ(say("openwindow", "55ce,2,kitty,~/src, again"), "kitty opened on workspace 2: \"~/src, again\"");
    EXPECT_EQ(say("closewindow", "55ce"), "kitty closed");
    EXPECT_EQ(say("closewindow", "abcd"), "window 0xabcd closed");
    EXPECT_EQ(say("workspacev2", "3,3"), "Switched to workspace 3");
    EXPECT_EQ(say("movewindowv2", "55ce,-98,special:magic"), "kitty moved to special workspace magic");
    EXPECT_EQ(say("changefloatingmode", "55ce,1"), "kitty floats");
    EXPECT_EQ(say("activewindowv2", ""), "No window has focus");
    EXPECT_EQ(say("submap", ""), "Back to the normal keybindings");
    EXPECT_EQ(say("somethingnew", "a,b"), "somethingnew: a,b");
    EXPECT_TRUE(application::repeatsNewerEvent("workspace"));
    EXPECT_FALSE(application::repeatsNewerEvent("workspacev2"));
}
