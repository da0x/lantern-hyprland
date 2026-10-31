// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/plugins-page.hpp"

#include "application/command.hpp"
#include "application/lantern-folder.hpp"
#include "application/settings.hpp"
#include "configuration/main-config.hpp"
#include "desktop/system.hpp"
#include "foundation/environment.hpp"
#include "ipc/instance.hpp"
#include "ui/markup.hpp"
#include "ui/texts.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <sstream>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprutils::Memory::CSharedPointer;

        std::optional<std::filesystem::path> hyprctl() {
            return desktop::findProgram("hyprctl", foundation::variable(foundation::currentEnvironment(), "PATH").value_or(""));
        }

        std::filesystem::path home() {
            return foundation::variable(foundation::currentEnvironment(), "HOME").value_or("");
        }

        // The plugin files the config loads, read without running anything.
        std::vector<std::filesystem::path> configured() {
            const auto config = configuration::mainConfig(configuration::lanternLaunch());
            if (!config)
                return {};
            if (config->lua()) {
                const auto recording = configuration::record(config->file);
                return recording ? application::pluginsInConfig(*recording, home()) : std::vector<std::filesystem::path>{};
            }
            std::ifstream     stream{config->file, std::ios::binary};
            std::stringstream text;
            text << stream.rdbuf();
            return application::pluginsInConfig(text.str(), home());
        }

        std::vector<std::filesystem::path> remembered() {
            const auto settings = application::loadSettings(application::lanternFolder(foundation::currentEnvironment()));
            return {settings.pluginsTurnedOff.begin(), settings.pluginsTurnedOff.end()};
        }

        // Adds `file` to the plugins Lantern turned off, or takes it out.
        std::expected<void, std::string> remember(const std::filesystem::path& file, bool turnedOff) {
            const auto folder   = application::lanternFolder(foundation::currentEnvironment());
            auto       settings = application::loadSettings(folder);
            std::erase(settings.pluginsTurnedOff, file.string());
            if (turnedOff)
                settings.pluginsTurnedOff.push_back(file.string());
            return application::saveSettings(settings, folder);
        }

    }

    PluginsPage::PluginsPage(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop) :
        palette{std::move(palette)}, defer{std::move(defer)}, loop{std::move(loop)} {
        root = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(10)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
        root->setMargin(28);
        root->addChild(pageTitle("Plugins", this->palette));
        root->addChild(paragraph("The plugins Hyprland has loaded, and the ones it can load. On and Off take effect at once and do not change what loads at login: a "
                                 "plugin your config loads comes back when Hyprland starts.",
                                 this->palette));
        root->addChild(paragraph("A plugin is code that runs inside Hyprland. One with a bug can crash Hyprland as it is turned on or off, closing every window, so "
                                 "save your work first.",
                                 this->palette, true));
        body = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(6)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 1.0}})->commence();
        body->setGrow(false, true);
        root->addChild(body);
        footer = leftRow();
        root->addChild(footer);
        refresh();
    }

    void PluginsPage::refresh() {
        const auto program = hyprctl();
        if (!program) {
            problem = "hyprctl is not installed, so Lantern cannot ask Hyprland for its plugins.";
            return show();
        }
        const auto started = application::runCommand(loop, *program, {"-j", "plugin", "list"}, [this](bool succeeded, std::string output) {
            asked             = true;
            const auto parsed = succeeded ? ipc::parseLoadedPlugins(output) : std::unexpected{output};
            if (!parsed) {
                problem = std::format("Hyprland did not list its plugins: {}", parsed.error());
                plugins.clear();
                return show();
            }
            problem.clear();
            const auto instance = ipc::currentInstance();
            plugins             = application::listPlugins(*parsed, instance ? ipc::mappedLibraries(*instance) : std::vector<std::filesystem::path>{}, configured(), remembered());
            show();
        });
        if (!started)
            problem = std::format("Lantern could not run hyprctl: {}", started.error());
        show();
    }

    void PluginsPage::restyle() {
        show();
    }

    void PluginsPage::show() {
        body->clearChildren();
        body->addChild(space(4));
        if (!problem.empty())
            body->addChild(paragraph(problem, palette));
        else if (asked && plugins.empty())
            body->addChild(paragraph("No plugins are loaded, and your config loads none.", palette));
        for (const auto& plugin : plugins)
            addPlugin(plugin);
        setFooter(footer, {}, outcome.empty() ? nullptr : CSharedPointer<Hyprtoolkit::IElement>{markupLine(escapeMarkup(outcome), palette)});
    }

    void PluginsPage::addPlugin(const application::Plugin& plugin) {
        const bool  busy = turning && plugin.file == turning;
        std::string shown;
        if (busy)
            shown = plugin.loaded ? "turning off…" : "turning on…";
        else
            shown = plugin.loaded ? "loaded" : "not loaded";
        const auto title = std::format("<span weight=\"600\">{}</span>{}   {}", escapeMarkup(plugin.name), plugin.version.empty() ? "" : "  " + escapeMarkup(plugin.version), shown);

        const auto row   = leftRow();
        const auto state = markupLine(title, palette);
        state->setGrow(true, false);
        row->addChild(state);
        if (plugin.file) {
            row->addChild(button("On", 70, plugin.loaded, [this, plugin] { defer([this, plugin] { turn(plugin, true); }); }));
            row->addChild(button("Off", 70, !plugin.loaded, [this, plugin] { defer([this, plugin] { turn(plugin, false); }); }));
            if (plugin.remembered && !plugin.loaded && !plugin.inConfig)
                row->addChild(button("Forget", 90, false, [this, file = *plugin.file] { defer([this, file] { forget(file); }); }));
        }
        body->addChild(row);

        if (!plugin.description.empty())
            body->addChild(paragraph(plugin.author.empty() ? plugin.description : std::format("{} By {}.", plugin.description, plugin.author), palette, true));
        if (plugin.file) {
            std::string where = shortPath(*plugin.file);
            if (plugin.inConfig)
                where += ", loaded by your config";
            else if (plugin.remembered && !plugin.loaded)
                where += ", turned off from Lantern";
            body->addChild(paragraph(where, palette, true));
        } else {
            body->addChild(paragraph("Lantern cannot tell which file this plugin was loaded from, so it cannot turn it off.", palette, true));
        }
        body->addChild(space(scaled(8.0)));
    }

    void PluginsPage::turn(const application::Plugin& plugin, bool on) {
        if (turning || !plugin.file || plugin.loaded == on)
            return;
        const auto program = hyprctl();
        if (!program)
            return;
        outcome.clear();
        turning            = plugin.file;
        const auto file    = *plugin.file;
        const auto name    = plugin.name;
        const auto started = application::runCommand(loop, *program, {"plugin", on ? "load" : "unload", file.string()}, [this, file, name, on](bool succeeded, std::string output) {
            turning.reset();
            // hyprctl says "ok" when it worked and the reason when it did not, and exits
            // the same either way.
            while (!output.empty() && std::isspace(static_cast<unsigned char>(output.back())))
                output.pop_back();
            if (succeeded && output == "ok") {
                if (const auto saved = remember(file, !on); !saved)
                    outcome = std::format("{} is {}, but Lantern could not save its settings: {}.", name, on ? "on" : "off", saved.error());
            } else {
                outcome = std::format("{} was not turned {}: {}", name, on ? "on" : "off", output.empty() ? "hyprctl did not say why." : output);
            }
            refresh();
        });
        if (!started) {
            turning.reset();
            outcome = std::format("Lantern could not run hyprctl: {}", started.error());
        }
        show();
    }

    void PluginsPage::forget(const std::filesystem::path& file) {
        if (const auto saved = remember(file, false); !saved)
            outcome = std::format("Lantern could not save its settings: {}.", saved.error());
        refresh();
    }

}
