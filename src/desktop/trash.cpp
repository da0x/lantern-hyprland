// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/trash.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <format>
#include <unistd.h>

namespace desktop {

    namespace {

        // A path as a URL writes it: every byte but letters, digits, / and -_.~ as %XX.
        std::string escaped(const std::string& path) {
            std::string text;
            for (const auto character : path) {
                const auto byte = static_cast<unsigned char>(character);
                if (std::isalnum(byte) != 0 || character == '/' || character == '-' || character == '_' || character == '.' || character == '~')
                    text += character;
                else
                    text += std::format("%{:02X}", byte);
            }
            return text;
        }

    }

    std::filesystem::path trashFolder(const foundation::Environment& environment) {
        if (const auto data = foundation::variable(environment, "XDG_DATA_HOME"); data && std::filesystem::path{*data}.is_absolute())
            return std::filesystem::path{*data} / "Trash";
        return std::filesystem::path{foundation::variable(environment, "HOME").value_or("")} / ".local" / "share" / "Trash";
    }

    std::expected<std::filesystem::path, std::string> moveToTrash(const std::filesystem::path& file, const std::filesystem::path& trash, std::chrono::system_clock::time_point when) {
        std::error_code error;
        const auto      source = std::filesystem::absolute(file, error);
        if (error || !std::filesystem::is_regular_file(source, error))
            return std::unexpected{std::format("{} is not a file", file.string())};
        std::filesystem::create_directories(trash / "files", error);
        std::filesystem::create_directories(trash / "info", error);
        if (error)
            return std::unexpected{std::format("cannot make the trash folder: {}", error.message())};

        const auto  time = std::chrono::system_clock::to_time_t(when);
        std::tm     local{};
        localtime_r(&time, &local);
        char date[32];
        std::strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S", &local);
        const auto info = std::format("[Trash Info]\nPath={}\nDeletionDate={}\n", escaped(source.string()), date);

        // The .trashinfo is made first, and only if its name is free, which claims the name.
        const auto stem      = source.stem().string();
        const auto extension = source.extension().string();
        for (int number = 1; number < 1000; ++number) {
            const auto name      = number == 1 ? source.filename().string() : std::format("{}.{}{}", stem, number, extension);
            const auto infoFile  = trash / "info" / (name + ".trashinfo");
            const int  made      = ::open(infoFile.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
            if (made < 0) {
                if (errno == EEXIST)
                    continue;
                return std::unexpected{std::format("cannot write to the trash: {}", std::strerror(errno))};
            }
            const bool written = ::write(made, info.data(), info.size()) == static_cast<ssize_t>(info.size());
            ::close(made);
            const auto      target = trash / "files" / name;
            std::error_code moved;
            if (written && !std::filesystem::exists(target, error)) {
                std::filesystem::rename(source, target, moved);
                if (!moved)
                    return target;
            }
            std::filesystem::remove(infoFile, error);
            if (moved == std::errc::cross_device_link)
                return std::unexpected{"it is on another drive than the trash"};
            if (!written)
                return std::unexpected{"cannot write to the trash"};
            if (moved)
                return std::unexpected{std::format("cannot move it to the trash: {}", moved.message())};
        }
        return std::unexpected{"the trash has too many files of that name"};
    }

}
