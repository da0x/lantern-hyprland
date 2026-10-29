// SPDX-License-Identifier: GPL-3.0-or-later
#include "ipc/bindings.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

namespace {

    std::string fixture(const char* name) {
        std::ifstream     file{std::string{FIXTURES} + "/" + name};
        std::stringstream text;
        text << file.rdbuf();
        return text.str();
    }

}

TEST(Bindings, ReadsHyprlandsReplyUnderLua) {
    const auto bindings = ipc::parseBindings(fixture("binds-lua.json"));
    ASSERT_TRUE(bindings) << bindings.error();
    ASSERT_EQ(bindings->size(), 3U);

    const auto& first = bindings->at(0);
    EXPECT_EQ(first.modifiers.with(first.key), "SUPER + Q");
    EXPECT_TRUE(first.fromLua());
    EXPECT_TRUE(first.description.empty());

    const auto& volume = bindings->at(1);
    EXPECT_EQ(volume.modifiers.with(volume.key), "XF86AudioRaiseVolume");
    EXPECT_EQ(volume.description, "Raise the volume");
    EXPECT_TRUE(volume.locked);
    EXPECT_TRUE(volume.repeat);

    const auto& drag = bindings->at(2);
    EXPECT_EQ(drag.modifiers.with(drag.key), "SUPER + SHIFT + mouse:272");
    EXPECT_EQ(drag.submap, "resize");
    EXPECT_TRUE(drag.mouse);
}

TEST(Bindings, ToleratesFieldsOfAnUnexpectedType) {
    const auto bindings = ipc::parseBindings(R"([{"modmask": "64", "key": 5, "locked": "true"}])");
    ASSERT_TRUE(bindings) << bindings.error();
    ASSERT_EQ(bindings->size(), 1U);
    EXPECT_TRUE(bindings->front().modifiers.empty());
    EXPECT_TRUE(bindings->front().key.empty());
    EXPECT_FALSE(bindings->front().locked);
}

TEST(Bindings, RejectsWhatIsNotAListOfObjects) {
    EXPECT_FALSE(ipc::parseBindings("not json"));
    EXPECT_FALSE(ipc::parseBindings(R"({"key": "Q"})"));
    EXPECT_FALSE(ipc::parseBindings(R"([1, 2])"));
    EXPECT_TRUE(ipc::parseBindings("[]"));
}

#include "application/keybindings.hpp"

TEST(Clashes, GroupsKeysBoundMoreThanOnceTheSameWay) {
    const auto row = [](std::uint32_t modifiers, std::string key, std::string submap = {}, bool release = false) {
        ipc::Binding live;
        live.modifiers = foundation::Modifiers{modifiers};
        live.key       = std::move(key);
        live.submap    = std::move(submap);
        live.release   = release;
        return application::KeybindingRow{.live = std::move(live), .recorded = std::nullopt};
    };
    const std::vector rows{
        row(64, "Q"),           // SUPER + Q
        row(64, "Return"),      //
        row(64, "q"),           // SUPER + Q again, in another case
        row(65, "Q"),           // SUPER + SHIFT + Q: other modifiers
        row(64, "Q", "resize"), // another submap
        row(64, "Q", {}, true), // on release
        row(64, "return"),      // SUPER + Return again
        row(64, "Q"),           // a third SUPER + Q
    };
    EXPECT_EQ(application::clashes(rows), (std::vector<std::vector<std::size_t>>{{0, 2, 7}, {1, 6}}));
}
