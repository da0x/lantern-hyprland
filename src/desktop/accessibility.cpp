// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/accessibility.hpp"

#include "configuration/hyprlang-edit.hpp"
#include "configuration/lua-edit.hpp"
#include "desktop/theme.hpp"
#include "syntax/highlight.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <regex>
#include <sstream>

namespace desktop {

    namespace {

        constexpr auto interfaceSchema = "org.gnome.desktop.interface";
        constexpr auto a11ySchema      = "org.gnome.desktop.a11y.interface";

        const std::vector<std::string_view> animationsPath{"animations", "enabled"};
        const std::vector<std::string_view> shaderPath{"decoration", "screen_shader"};

        // hyprtoolkit's own text sizes, from its ConfigManager.
        constexpr std::array<std::pair<std::string_view, int>, 5> toolkitDefaults{{
            {"h1_size", 19},
            {"h2_size", 15},
            {"h3_size", 13},
            {"font_size", 11},
            {"small_font_size", 10},
        }};

        std::string readAll(const std::filesystem::path& file) {
            std::ifstream     stream{file, std::ios::binary};
            std::stringstream text;
            text << stream.rdbuf();
            return text.str();
        }

        std::filesystem::path configHome(const foundation::Environment& environment) {
            if (const auto home = foundation::variable(environment, "XDG_CONFIG_HOME"); home && std::filesystem::path{*home}.is_absolute())
                return *home;
            return std::filesystem::path{foundation::variable(environment, "HOME").value_or("")} / ".config";
        }

        template <typename Number>
        std::optional<Number> numberOf(std::string_view text) {
            Number value{};
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size())
                return std::nullopt;
            return value;
        }

        // A value hl.config writes in place, as its Lua source.
        std::optional<std::string> luaValue(std::string_view config, const std::vector<std::string_view>& path) {
            const auto found = syntax::luaCallField(config, "hl.config", path);
            if (!found)
                return std::nullopt;
            return std::string{config.substr(found->begin, found->end - found->begin)};
        }

        std::string quoted(const std::filesystem::path& file) {
            return std::format("\"{}\"", file.string());
        }

        // A color-blind eye, in linear RGB: Machado, Oliveira and Fernandes (2009) at full
        // strength. Rows of the matrix that turns a color into the color seen.
        struct Correction {
            std::array<std::array<double, 3>, 3> seen;
            // Where what is lost goes: the error's share added to each channel.
            std::array<std::array<double, 3>, 3> shift;
            std::string_view                     who;
        };

        Correction correctionFor(Vision vision) {
            if (vision == Vision::blueYellow)
                return {{{{1.255528, -0.076749, -0.178779}, {-0.078411, 0.930809, 0.147602}, {0.004733, 0.691367, 0.303900}}},
                        {{{1, 0, 0.7}, {0, 1, 0.7}, {0, 0, 0}}},
                        "blue-yellow color blindness (tritanopia)"};
            // Deuteranopia, the most common; it serves protanopia nearly as well.
            return {{{{0.367322, 0.860646, -0.227968}, {0.280085, 0.672501, 0.047413}, {-0.011820, 0.042940, 0.968881}}},
                    {{{0, 0, 0}, {0.7, 1, 0}, {0.7, 0, 1}}},
                    "red-green color blindness (deuteranopia and protanopia)"};
        }

    }

    std::string_view nameOf(Contrast contrast) {
        return contrast == Contrast::high ? "high" : "standard";
    }

    std::string_view nameOf(Vision vision) {
        switch (vision) {
            case Vision::redGreen: return "red-green";
            case Vision::blueYellow: return "blue-yellow";
            case Vision::typical: break;
        }
        return "typical";
    }

    std::optional<Contrast> contrastNamed(std::string_view name) {
        for (const auto contrast : {Contrast::standard, Contrast::high})
            if (nameOf(contrast) == name)
                return contrast;
        return std::nullopt;
    }

    std::optional<Vision> visionNamed(std::string_view name) {
        for (const auto vision : {Vision::typical, Vision::redGreen, Vision::blueYellow})
            if (nameOf(vision) == name)
                return vision;
        return std::nullopt;
    }

    std::string colorVisionShader(Vision vision) {
        if (vision == Vision::typical)
            return {};
        const auto correction = correctionFor(vision);
        const auto row        = [](const std::array<double, 3>& values) { return std::format("vec3({:.6f}, {:.6f}, {:.6f})", values[0], values[1], values[2]); };
        return std::format(R"x(#version 300 es
// Color correction for {}, written by Lantern.
// Colors the eye cannot tell apart are moved to colors it can: the color as it would be
// seen is worked out (Machado, Oliveira and Fernandes, 2009), and what is lost is added to
// the channels that are seen.

precision highp float;
in vec2 v_texcoord;
uniform sampler2D tex;
layout(location = 0) out vec4 fragColor;

vec3 toLinear(vec3 color) {{
    return mix(color / 12.92, pow((color + 0.055) / 1.055, vec3(2.4)), step(0.04045, color));
}}

vec3 toScreen(vec3 color) {{
    return mix(color * 12.92, 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, color));
}}

void main() {{
    vec4 pixel  = texture(tex, v_texcoord);
    vec3 color = toLinear(pixel.rgb);
    vec3 seen   = vec3(dot({}, color), dot({}, color), dot({}, color));
    vec3 lost   = color - seen;
    vec3 moved  = vec3(dot({}, lost), dot({}, lost), dot({}, lost));
    fragColor   = vec4(toScreen(clamp(color + moved, 0.0, 1.0)), pixel.a);
}}
)x",
                           correction.who, row(correction.seen[0]), row(correction.seen[1]), row(correction.seen[2]), row(correction.shift[0]), row(correction.shift[1]),
                           row(correction.shift[2]));
    }

    Color corrected(const Color& color, Vision vision) {
        if (vision == Vision::typical)
            return color;
        const auto toLinear = [](double channel) { return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4); };
        const auto toScreen = [](double channel) { return channel <= 0.0031308 ? channel * 12.92 : 1.055 * std::pow(channel, 1.0 / 2.4) - 0.055; };
        const auto correction = correctionFor(vision);
        const std::array<double, 3> linear{toLinear(color.red), toLinear(color.green), toLinear(color.blue)};
        const auto dot = [](const std::array<double, 3>& row, const std::array<double, 3>& values) { return row[0] * values[0] + row[1] * values[1] + row[2] * values[2]; };
        std::array<double, 3> lost{};
        for (std::size_t channel = 0; channel < 3; ++channel)
            lost[channel] = linear[channel] - dot(correction.seen[channel], linear);
        std::array<double, 3> result{};
        for (std::size_t channel = 0; channel < 3; ++channel)
            result[channel] = toScreen(std::clamp(linear[channel] + dot(correction.shift[channel], lost), 0.0, 1.0));
        return {result[0], result[1], result[2]};
    }

    std::filesystem::path colorVisionShaderFile(Vision vision, const foundation::Environment& environment) {
        return configHome(environment) / "hypr" / "shaders" / std::format("color-vision-{}.frag", nameOf(vision));
    }

    std::vector<std::pair<std::string, std::string>> toolkitTextSizes(double scale) {
        std::vector<std::pair<std::string, std::string>> sizes;
        for (const auto& [key, size] : toolkitDefaults)
            sizes.emplace_back(std::string{key}, std::to_string(std::lround(size * scale)));
        return sizes;
    }

    Accessibility currentAccessibility(const configuration::MainConfig& config, const foundation::Environment& environment) {
        Accessibility current;
        current.contrast = gsetting(a11ySchema, "high-contrast", environment) == "true" ? Contrast::high : Contrast::standard;
        if (const auto scale = gsetting(interfaceSchema, "text-scaling-factor", environment))
            current.textScale = numberOf<double>(*scale).value_or(1.0);
        if (const auto size = gsetting(interfaceSchema, "cursor-size", environment))
            current.cursorSize = numberOf<int>(*size).value_or(24);
        current.reduceMotion = gsetting(a11ySchema, "reduced-motion", environment) == "'reduce'";

        std::error_code error;
        const auto      file = std::filesystem::canonical(config.file, error);
        if (error || !config.lua())
            return current;
        const auto text = readAll(file);
        // Hyprland's own say wins for what it draws.
        if (const auto size = configuration::luaEnv(text, "XCURSOR_SIZE"))
            current.cursorSize = numberOf<int>(*size).value_or(current.cursorSize);
        if (luaValue(text, animationsPath) == "false")
            current.reduceMotion = true;
        if (const auto shader = luaValue(text, shaderPath))
            for (const auto vision : {Vision::redGreen, Vision::blueYellow})
                if (*shader == quoted(colorVisionShaderFile(vision, environment))) {
                    current.vision       = vision;
                    current.wholeDesktop = true;
                }
        return current;
    }

    std::expected<AccessibilityPlan, std::string> planAccessibility(const Accessibility& wanted, const configuration::MainConfig& config,
                                                                    const foundation::Environment& environment) {
        AccessibilityPlan plan;
        plan.settings = gsettingChanges(
            {
                {a11ySchema, "high-contrast", "", wanted.contrast == Contrast::high ? "true" : "false"},
                {a11ySchema, "reduced-motion", "", wanted.reduceMotion ? "'reduce'" : "'no-preference'"},
                {interfaceSchema, "enable-animations", "", wanted.reduceMotion ? "false" : "true"},
                {interfaceSchema, "text-scaling-factor", "", std::format("{:g}", wanted.textScale)},
                {interfaceSchema, "cursor-size", "", std::to_string(wanted.cursorSize)},
            },
            environment);

        if (!config.lua())
            return std::unexpected{"the accessibility settings change Hyprland only in a Lua config (hyprland.lua) so far"};
        std::error_code error;
        const auto      file = std::filesystem::canonical(config.file, error);
        if (error)
            return std::unexpected{std::format("cannot read {}: {}", config.file.string(), error.message())};
        auto hyprland = configuration::planFile(file, "");
        if (!hyprland)
            return std::unexpected{hyprland.error()};
        auto text = hyprland->current.value_or("");

        // Animations: turned off where asked, and back on only where the config says off.
        if (wanted.reduceMotion)
            text = configuration::withLuaConfigValueSet(text, animationsPath, "false");
        else if (auto edited = configuration::withLuaConfigValue(text, animationsPath, "true"))
            text = std::move(*edited);

        // The cursor's size, for Hyprland and for the apps it starts; left out at the
        // default when the config never said.
        for (const auto* name : {"XCURSOR_SIZE", "HYPRCURSOR_SIZE"})
            if (wanted.cursorSize != 24 || configuration::luaEnv(text, name))
                text = configuration::withLuaEnv(text, name, std::to_string(wanted.cursorSize));

        // The screen shader: Lantern's, or none where Lantern's was; a shader of the
        // user's own is left alone unless correction is asked for.
        const bool shaded = wanted.wholeDesktop && wanted.vision != Vision::typical;
        if (shaded) {
            text           = configuration::withLuaConfigValueSet(text, shaderPath, quoted(colorVisionShaderFile(wanted.vision, environment)));
            auto shaderFile = configuration::planEdit(colorVisionShaderFile(wanted.vision, environment), [&wanted](std::string_view) { return colorVisionShader(wanted.vision); });
            if (!shaderFile)
                return std::unexpected{shaderFile.error()};
            if (*shaderFile)
                plan.files.push_back(std::move(**shaderFile));
        } else if (const auto shader = luaValue(text, shaderPath)) {
            for (const auto vision : {Vision::redGreen, Vision::blueYellow})
                if (*shader == quoted(colorVisionShaderFile(vision, environment)))
                    if (auto edited = configuration::withLuaConfigValue(text, shaderPath, "\"\""))
                        text = std::move(*edited);
        }
        hyprland->proposed = std::move(text);
        if (hyprland->changes())
            plan.files.insert(plan.files.begin(), std::move(*hyprland));

        // hyprtoolkit's text sizes, written once the scale is not 1 or the file has them.
        const auto toolkitFile = hyprtoolkitConfigFile(environment);
        static const std::regex sizeLine{R"(^[ \t]*font_size[ \t]*=)", std::regex::multiline};
        const bool sized       = std::abs(wanted.textScale - 1.0) > 1e-9 || std::regex_search(readAll(toolkitFile), sizeLine);
        if (sized) {
            auto toolkit = configuration::planEdit(toolkitFile, [&wanted](std::string_view current) { return configuration::withHyprlangValues(current, toolkitTextSizes(wanted.textScale)); });
            if (!toolkit)
                return std::unexpected{toolkit.error()};
            if (*toolkit)
                plan.files.push_back(std::move(**toolkit));
        }
        return plan;
    }

}
