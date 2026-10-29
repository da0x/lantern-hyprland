// SPDX-License-Identifier: GPL-3.0-or-later
#include "configuration/file-change.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace configuration {

    namespace {

        constexpr auto manifestName = "change.txt";
        constexpr auto undoneName   = "undone";

        std::string errorText(int number) {
            return std::strerror(number); // NOLINT(concurrency-mt-unsafe): one thread writes
        }

        std::expected<std::string, std::string> readWhole(const std::filesystem::path& file) {
            std::ifstream stream{file, std::ios::binary};
            if (!stream)
                return std::unexpected{std::format("cannot read {}", file.string())};
            std::stringstream text;
            text << stream.rdbuf();
            return text.str();
        }

        std::filesystem::path newRecord(const std::filesystem::path& backups) {
            const auto now   = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())};
            const auto stamp = std::format("{:%Y-%m-%d-%H%M%S}", now);
            auto       record = backups / stamp;
            for (int number = 2; std::filesystem::exists(record); ++number)
                record = backups / std::format("{}-{}", stamp, number);
            return record;
        }

        struct Entry {
            bool                  created = false;
            std::filesystem::path file;
        };

        // change.txt: the description, then one line per file, "replaced PATH" or
        // "created PATH", in the order of the numbered copies beside it.
        std::vector<Entry> readManifest(const std::filesystem::path& record) {
            std::vector<Entry> entries;
            std::ifstream      stream{record / manifestName};
            std::string        line;
            std::getline(stream, line);
            while (std::getline(stream, line)) {
                if (line.starts_with("replaced "))
                    entries.push_back({false, line.substr(9)});
                else if (line.starts_with("created "))
                    entries.push_back({true, line.substr(8)});
            }
            return entries;
        }

        std::filesystem::path copyOf(const std::filesystem::path& record, std::size_t index, std::string_view which) {
            return record / std::format("{}.{}", index + 1, which);
        }

    }

    std::expected<void, std::string> writeWhole(const std::filesystem::path& file, std::string_view text) {
        std::error_code error;
        std::filesystem::create_directories(file.parent_path(), error);
        if (error)
            return std::unexpected{std::format("cannot create {}: {}", file.parent_path().string(), error.message())};

        mode_t mode = 0644;
        if (struct stat existing{}; ::stat(file.c_str(), &existing) == 0)
            mode = existing.st_mode & 07777;

        const auto temporary  = file.parent_path() / std::format(".{}.lantern-hyprland-{}", file.filename().string(), ::getpid());
        const int  descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode); // NOLINT(cppcoreguidelines-pro-type-vararg): POSIX
        if (descriptor < 0)
            return std::unexpected{std::format("cannot write {}: {}", temporary.string(), errorText(errno))};
        std::size_t written = 0;
        bool        failed  = false;
        while (written < text.size()) {
            const auto count = ::write(descriptor, text.data() + written, text.size() - written);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0) {
                failed = true;
                break;
            }
            written += static_cast<std::size_t>(count);
        }
        failed = failed || ::fchmod(descriptor, mode) != 0 || ::fsync(descriptor) != 0;
        const int saved = errno;
        ::close(descriptor);
        if (failed || ::rename(temporary.c_str(), file.c_str()) != 0) {
            const auto reason = errorText(failed ? saved : errno);
            std::filesystem::remove(temporary, error);
            return std::unexpected{std::format("cannot write {}: {}", file.string(), reason)};
        }
        return {};
    }

    std::expected<FileChange, std::string> planFile(const std::filesystem::path& file, std::string proposed) {
        std::error_code error;
        const auto      status = std::filesystem::symlink_status(file, error);
        if (status.type() == std::filesystem::file_type::symlink)
            return std::unexpected{std::format("{} is a link, which Lantern does not replace", file.string())};
        if (status.type() == std::filesystem::file_type::not_found)
            return FileChange{file, std::nullopt, std::move(proposed)};
        if (status.type() != std::filesystem::file_type::regular)
            return std::unexpected{std::format("{} is not a regular file", file.string())};
        auto current = readWhole(file);
        if (!current)
            return std::unexpected{current.error()};
        return FileChange{file, std::move(*current), std::move(proposed)};
    }

    std::expected<std::optional<FileChange>, std::string> planEdit(std::filesystem::path file, const std::function<std::string(std::string_view)>& edit, std::string_view start) {
        std::error_code error;
        if (std::filesystem::is_symlink(file, error)) {
            file = std::filesystem::canonical(file, error);
            if (error)
                return std::unexpected{std::format("cannot follow {}: {}", file.string(), error.message())};
        }
        auto change = planFile(file, "");
        if (!change)
            return std::unexpected{change.error()};
        change->proposed = edit(change->current ? std::string_view{*change->current} : start);
        if (!change->changes())
            return std::nullopt;
        return std::optional{std::move(*change)};
    }

    std::expected<std::filesystem::path, std::string> applyChanges(const std::vector<FileChange>& changes, const std::filesystem::path& backups, std::string_view description) {
        // Nothing is written unless every file is still what the user reviewed.
        for (const auto& change : changes) {
            const auto now = planFile(change.file, change.proposed);
            if (!now)
                return std::unexpected{now.error()};
            if (now->current != change.current)
                return std::unexpected{std::format("{} changed after it was reviewed; review it again", change.file.string())};
        }

        const auto      record = newRecord(backups);
        std::error_code error;
        std::filesystem::create_directories(record, error);
        if (error)
            return std::unexpected{std::format("cannot create {}: {}", record.string(), error.message())};
        std::string manifest = std::format("{}\n", description);
        for (std::size_t index = 0; index < changes.size(); ++index) {
            const auto& change = changes[index];
            if (change.current) {
                if (const auto saved = writeWhole(copyOf(record, index, "before"), *change.current); !saved)
                    return std::unexpected{saved.error()};
            }
            if (const auto saved = writeWhole(copyOf(record, index, "after"), change.proposed); !saved)
                return std::unexpected{saved.error()};
            manifest += std::format("{} {}\n", change.current ? "replaced" : "created", change.file.string());
        }
        if (const auto saved = writeWhole(record / manifestName, manifest); !saved)
            return std::unexpected{saved.error()};

        for (std::size_t index = 0; index < changes.size(); ++index) {
            if (const auto written = writeWhole(changes[index].file, changes[index].proposed); !written) {
                // Put back what was already written, newest first.
                for (std::size_t back = index; back-- > 0;) {
                    if (changes[back].current)
                        (void)writeWhole(changes[back].file, *changes[back].current);
                    else
                        std::filesystem::remove(changes[back].file, error);
                }
                std::filesystem::remove_all(record, error);
                return std::unexpected{written.error()};
            }
        }
        return record;
    }

    std::expected<void, std::string> undoChange(const std::filesystem::path& record) {
        const auto entries = readManifest(record);
        if (!std::filesystem::exists(record / manifestName))
            return std::unexpected{std::format("{} records no change", record.string())};
        // Refuse before touching anything if a file was edited since.
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const auto written = readWhole(copyOf(record, index, "after"));
            const auto now     = readWhole(entries[index].file);
            if (!written || !now || *written != *now)
                return std::unexpected{std::format("{} was changed after Lantern wrote it, so it is left as it is", entries[index].file.string())};
        }
        for (std::size_t index = 0; index < entries.size(); ++index) {
            std::error_code error;
            if (entries[index].created) {
                std::filesystem::remove(entries[index].file, error);
                if (error)
                    return std::unexpected{std::format("cannot remove {}: {}", entries[index].file.string(), error.message())};
                continue;
            }
            const auto before = readWhole(copyOf(record, index, "before"));
            if (!before)
                return std::unexpected{before.error()};
            if (const auto restored = writeWhole(entries[index].file, *before); !restored)
                return std::unexpected{restored.error()};
        }
        std::ofstream{record / undoneName} << "undone\n";
        return {};
    }

    std::optional<std::filesystem::path> latestChange(const std::filesystem::path& backups, std::string_view about) {
        std::vector<std::filesystem::path> records;
        std::error_code                    error;
        for (const auto& entry : std::filesystem::directory_iterator{backups, error})
            if (entry.is_directory() && std::filesystem::exists(entry.path() / manifestName) && !std::filesystem::exists(entry.path() / undoneName)) {
                std::ifstream stream{entry.path() / manifestName};
                std::string   description;
                std::getline(stream, description);
                if (description.starts_with(about))
                    records.push_back(entry.path());
            }
        if (records.empty())
            return std::nullopt;
        // The names start with the time, so the last in order is the newest.
        return *std::ranges::max_element(records);
    }

}
