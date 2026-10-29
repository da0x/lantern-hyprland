// SPDX-License-Identifier: GPL-3.0-or-later
#include "application/settings.hpp"

#include "configuration/file-change.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace application {

    namespace {

        nlohmann::json readFile(const std::filesystem::path& lanternFolder) {
            std::ifstream stream{lanternFolder / "settings.json"};
            if (!stream)
                return nlohmann::json::object();
            auto json = nlohmann::json::parse(stream, nullptr, false);
            return json.is_object() ? json : nlohmann::json::object();
        }

    }

    Settings loadSettings(const std::filesystem::path& lanternFolder) {
        const auto json = readFile(lanternFolder);
        Settings   settings;
        if (const auto bar = json.find("bar"); bar != json.end() && bar->is_object())
            settings.barRequirementsDismissed = bar->value("requirementsDismissed", false);
        if (const auto wallpaper = json.find("wallpaper"); wallpaper != json.end() && wallpaper->is_object()) {
            settings.wallpaperRequirementsDismissed = wallpaper->value("requirementsDismissed", false);
            settings.wallpaperProgram               = wallpaper->value("program", std::string{});
            settings.awwwFit                        = wallpaper->value("awwwFit", std::string{"cover"});
        }
        if (const auto accessibility = json.find("accessibility"); accessibility != json.end() && accessibility->is_object()) {
            settings.contrast = accessibility->value("contrast", std::string{});
            settings.vision   = accessibility->value("vision", std::string{});
        }
        if (const auto activation = json.find("activation"); activation != json.end() && activation->is_object())
            if (const auto allowed = activation->find("allowed"); allowed != activation->end() && allowed->is_array())
                for (const auto& name : *allowed)
                    if (name.is_string())
                        settings.activationAllowed.push_back(name.get<std::string>());
        return settings;
    }

    std::expected<void, std::string> saveSettings(const Settings& settings, const std::filesystem::path& lanternFolder) {
        auto json = readFile(lanternFolder);
        for (const auto* page : {"bar", "wallpaper", "accessibility", "activation"})
            if (!json[page].is_object())
                json[page] = nlohmann::json::object();
        json["bar"]["requirementsDismissed"]       = settings.barRequirementsDismissed;
        json["wallpaper"]["requirementsDismissed"] = settings.wallpaperRequirementsDismissed;
        json["wallpaper"]["program"]               = settings.wallpaperProgram;
        json["wallpaper"]["awwwFit"]               = settings.awwwFit;
        json["accessibility"]["contrast"]          = settings.contrast;
        json["accessibility"]["vision"]            = settings.vision;
        json["activation"]["allowed"]              = settings.activationAllowed;
        return configuration::writeWhole(lanternFolder / "settings.json", json.dump(4) + "\n");
    }

}
