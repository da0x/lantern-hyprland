// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace configuration {

    // A file Lantern proposes to write, with what is in it now.
    struct FileChange {
        std::filesystem::path      file;
        // Nothing when the file does not exist yet.
        std::optional<std::string> current;
        std::string                proposed;

        [[nodiscard]] bool         changes() const { return current != proposed; }
    };

    // Writes `text` to `file` whole, through a temporary file beside it and a rename, so the
    // file is never half written. Keeps the permissions of a file that is already there.
    [[nodiscard]] std::expected<void, std::string> writeWhole(const std::filesystem::path& file, std::string_view text);

    // Reads what `file` holds now. Refuses a symbolic link, which Lantern never replaces
    // with a real file, and anything else that is not a regular file.
    [[nodiscard]] std::expected<FileChange, std::string> planFile(const std::filesystem::path& file, std::string proposed);

    // The change to `file` that `edit` makes of what it holds now, or of `start` when it does
    // not exist; nothing when the edit changes nothing. A symbolic link is followed: the
    // file it points at is the one changed, and the link stays.
    [[nodiscard]] std::expected<std::optional<FileChange>, std::string> planEdit(std::filesystem::path file, const std::function<std::string(std::string_view)>& edit,
                                                                                 std::string_view start = {});

    // Writes the files the user reviewed. First it checks that each still holds what was
    // reviewed, and copies it into a new folder under `backups`, with a list of what is
    // done (change.txt). Then it writes each file whole, through a temporary file and a
    // rename, so a file is never half written. If a write fails, the files already written
    // are put back. Returns the new folder.
    [[nodiscard]] std::expected<std::filesystem::path, std::string> applyChanges(const std::vector<FileChange>& changes, const std::filesystem::path& backups,
                                                                                 std::string_view description);

    // Puts back the files of one applied change: a replaced file gets its old content, a
    // created one is removed. Refuses, changing nothing, if any of them was edited since. A
    // change of no files (what it changed lies outside them, recorded beside change.txt) is
    // simply marked undone.
    [[nodiscard]] std::expected<void, std::string> undoChange(const std::filesystem::path& record);

    // The newest change under `backups` that has not been undone, among those whose
    // description starts with `about`.
    [[nodiscard]] std::optional<std::filesystem::path> latestChange(const std::filesystem::path& backups, std::string_view about = {});

}
