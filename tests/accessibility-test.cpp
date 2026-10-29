// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/file-change.hpp"
#include "desktop/accessibility.hpp"
#include "desktop/system.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

namespace {

    std::filesystem::path freshRoot() {
        static std::atomic<int> counter = 0;
        const auto root = std::filesystem::temp_directory_path() / ("lantern-hyprland-access-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
        std::filesystem::create_directories(root / "home/.config/hypr");
        return root;
    }

    TEST(Accessibility, NamesEachChoiceAndReadsItBack) {
        for (const auto contrast : {desktop::Contrast::standard, desktop::Contrast::high})
            EXPECT_EQ(desktop::contrastNamed(desktop::nameOf(contrast)), contrast);
        for (const auto vision : {desktop::Vision::typical, desktop::Vision::redGreen, desktop::Vision::blueYellow})
            EXPECT_EQ(desktop::visionNamed(desktop::nameOf(vision)), vision);
        EXPECT_FALSE(desktop::visionNamed("").has_value());
    }

    TEST(Accessibility, PlansHyprlandToolkitAndShaderAndReadsThemBack) {
        const auto root = freshRoot();
        const auto lua  = root / "home/.config/hypr/hyprland.lua";
        std::ofstream{lua} << "hl.env(\"XCURSOR_SIZE\", \"24\")\nhl.config({\n    decoration = {\n        rounding = 10,\n    },\n    animations = {\n        enabled = true,\n    },\n})\n";
        // No PATH, so no gsettings: only the files are planned.
        const foundation::Environment   environment{{"HOME", (root / "home").string()}};
        const configuration::MainConfig config{.file = lua, .choice = configuration::Choice::search, .safeMode = false};

        EXPECT_EQ(desktop::currentAccessibility(config, environment), desktop::Accessibility{});
        // As it is: nothing to do.
        const auto same = desktop::planAccessibility({}, config, environment);
        ASSERT_TRUE(same.has_value()) << same.error();
        EXPECT_TRUE(same->empty());

        const desktop::Accessibility wanted{.contrast     = desktop::Contrast::high,
                                            .vision       = desktop::Vision::redGreen,
                                            .wholeDesktop = true,
                                            .textScale    = 1.25,
                                            .cursorSize   = 48,
                                            .reduceMotion = true};
        const auto planned = desktop::planAccessibility(wanted, config, environment);
        ASSERT_TRUE(planned.has_value()) << planned.error();
        ASSERT_EQ(planned->files.size(), 3U);
        const auto& hyprland = planned->files[0].proposed;
        EXPECT_TRUE(hyprland.contains("hl.env(\"XCURSOR_SIZE\", \"48\")\n"));
        EXPECT_TRUE(hyprland.contains("hl.env(\"HYPRCURSOR_SIZE\", \"48\")\n"));
        EXPECT_TRUE(hyprland.contains("        enabled = false,\n"));
        const auto shader = desktop::colorVisionShaderFile(desktop::Vision::redGreen, environment);
        EXPECT_TRUE(hyprland.ends_with(std::format("\nhl.config({{ decoration = {{ screen_shader = \"{}\" }} }})\n", shader.string())));
        EXPECT_EQ(planned->files[1].file, shader);
        EXPECT_EQ(planned->files[1].proposed, desktop::colorVisionShader(desktop::Vision::redGreen));
        EXPECT_TRUE(planned->files[2].proposed.contains("font_size = 14\n"));
        EXPECT_TRUE(planned->files[2].proposed.contains("h1_size = 24\n"));

        ASSERT_TRUE(configuration::applyChanges(planned->files, root / "backups", "Accessibility").has_value());
        const auto read = desktop::currentAccessibility(config, environment);
        EXPECT_EQ(read.vision, desktop::Vision::redGreen);
        EXPECT_TRUE(read.wholeDesktop);
        EXPECT_EQ(read.cursorSize, 48);
        EXPECT_TRUE(read.reduceMotion);

        // And back: the shader is let go, animations on, sizes as designed.
        const auto back = desktop::planAccessibility({}, config, environment);
        ASSERT_TRUE(back.has_value()) << back.error();
        ASSERT_EQ(back->files.size(), 2U);
        EXPECT_TRUE(back->files[0].proposed.ends_with("screen_shader = \"\" } })\n"));
        EXPECT_TRUE(back->files[0].proposed.contains("        enabled = true,\n"));
        EXPECT_TRUE(back->files[0].proposed.contains("hl.env(\"HYPRCURSOR_SIZE\", \"24\")\n"));
        EXPECT_TRUE(back->files[1].proposed.contains("font_size = 11\n"));
        ASSERT_TRUE(configuration::applyChanges(back->files, root / "backups", "Accessibility").has_value());
        EXPECT_EQ(desktop::currentAccessibility(config, environment), desktop::Accessibility{});
        std::filesystem::remove_all(root);
    }

    TEST(Accessibility, CorrectsColorsAsTheShaderDoes) {
        const desktop::Color red{1, 0, 0}, green{0, 0.6, 0};
        EXPECT_EQ(desktop::corrected(red, desktop::Vision::typical), red);
        // Red and green, alike to a red-green color-blind eye, end up apart in blue.
        const auto fixedRed = desktop::corrected(red, desktop::Vision::redGreen), fixedGreen = desktop::corrected(green, desktop::Vision::redGreen);
        EXPECT_GT(std::abs(fixedRed.blue - fixedGreen.blue), 0.3);
        // Gray is seen as it is, so nothing moves.
        const desktop::Color gray{0.5, 0.5, 0.5};
        const auto            same = desktop::corrected(gray, desktop::Vision::redGreen);
        EXPECT_NEAR(same.red, 0.5, 0.01);
        EXPECT_NEAR(same.blue, 0.5, 0.01);
    }

    TEST(Accessibility, ShadersCompile) {
        const auto validator = desktop::findProgram("glslangValidator", std::getenv("PATH") != nullptr ? std::getenv("PATH") : ""); // NOLINT(concurrency-mt-unsafe): read once
        if (!validator)
            GTEST_SKIP() << "glslangValidator is not installed";
        const auto root = freshRoot();
        for (const auto vision : {desktop::Vision::redGreen, desktop::Vision::blueYellow}) {
            const auto file = root / std::format("{}.frag", desktop::nameOf(vision));
            std::ofstream{file} << desktop::colorVisionShader(vision);
            EXPECT_EQ(std::system(std::format("'{}' '{}' > /dev/null", validator->string(), file.string()).c_str()), 0) << desktop::nameOf(vision); // NOLINT(cert-env33-c): a test's own tool
        }
        EXPECT_TRUE(desktop::colorVisionShader(desktop::Vision::typical).empty());
        std::filesystem::remove_all(root);
    }

}
