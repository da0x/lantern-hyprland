// SPDX-License-Identifier: GPL-3.0-or-later
#include "activation/record.hpp"

#include "configuration/file-change.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace activation {

    namespace {

        std::filesystem::path recordFile(const std::filesystem::path& lanternFolder) {
            return lanternFolder / "activation.json";
        }

    }

    std::optional<Record> loadRecord(const std::filesystem::path& lanternFolder) {
        std::ifstream stream{recordFile(lanternFolder)};
        if (!stream)
            return std::nullopt;
        const auto json = nlohmann::json::parse(stream, nullptr, false);
        if (!json.is_object())
            return std::nullopt;
        Record record;
        record.since = json.value("since", std::string{});
        if (const auto areas = json.find("areas"); areas != json.end() && areas->is_array())
            for (const auto& name : *areas)
                if (name.is_string())
                    if (const auto area = areaNamed(name.get<std::string>()))
                        record.areas.push_back(*area);
        if (const auto programs = json.find("programs"); programs != json.end() && programs->is_array())
            for (const auto& program : *programs)
                if (program.is_object())
                    record.programs.push_back({.process = program.value("process", 0), .name = program.value("name", std::string{}), .command = program.value("command", std::string{})});
        return record;
    }

    std::expected<void, std::string> saveRecord(const Record& record, const std::filesystem::path& lanternFolder) {
        nlohmann::json json{{"since", record.since}, {"areas", nlohmann::json::array()}, {"programs", nlohmann::json::array()}};
        for (const auto area : record.areas)
            json["areas"].push_back(std::string{nameOf(area)});
        for (const auto& program : record.programs)
            json["programs"].push_back({{"process", program.process}, {"name", program.name}, {"command", program.command}});
        std::error_code error;
        std::filesystem::create_directories(lanternFolder, error);
        return configuration::writeWhole(recordFile(lanternFolder), json.dump(4) + "\n");
    }

    void removeRecord(const std::filesystem::path& lanternFolder) {
        std::error_code error;
        std::filesystem::remove(recordFile(lanternFolder), error);
    }

}
