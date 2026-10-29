// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace configuration {

    // A hyprlang file (hyprtoolkit.conf, hyprpaper.conf) with each `key = value` outside
    // any block set to the value given, and every other byte as it was: an existing line
    // keeps its spacing and comment, and a key the file lacks is added at the end.
    [[nodiscard]] std::string withHyprlangValues(std::string_view config, const std::vector<std::pair<std::string, std::string>>& values);

    // Whether `config` has a line that is exactly `line`, apart from spacing around it.
    [[nodiscard]] bool hasLine(std::string_view config, std::string_view line);

    // A kitty-style file (`key value` lines) with `key` set to `value`, every other byte as it
    // was: the last line that sets the key keeps its indentation, and a missing key is added
    // at the end.
    [[nodiscard]] std::string withSpacedValue(std::string_view config, std::string_view key, std::string_view value);

    // The value the last `key value` line gives, if any.
    [[nodiscard]] std::optional<std::string> spacedValue(std::string_view config, std::string_view key);

    struct IniValue {
        std::string section;
        std::string key;
        std::string value;
    };

    // An INI file (dunstrc) with each key set in its [section], and every other byte as it
    // was: a line that sets it, commented out or not, keeps its indentation and comment;
    // a key the section lacks is added at the end of the section, and a missing section at
    // the end of the file.
    [[nodiscard]] std::string withIniValues(std::string_view config, const std::vector<IniValue>& values);

    // A block at the top of a hyprlang file: `name {` to its `}`, with its `key = value`
    // lines, comments left out. Blocks inside it are skipped.
    struct HyprlangBlock {
        std::string                                      name;
        std::size_t                                      begin = 0;
        // Past the newline after its `}`.
        std::size_t                                      end   = 0;
        // Where its `}` line starts.
        std::size_t                                      close = 0;
        std::vector<std::pair<std::string, std::string>> values;
        // Where each of those lines starts.
        std::vector<std::size_t>                         valueLines;
    };

    [[nodiscard]] std::vector<HyprlangBlock> hyprlangBlocks(std::string_view config);

}
