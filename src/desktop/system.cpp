// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop/system.hpp"

#include <fontconfig/fontconfig.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <system_error>

#include <unistd.h>

namespace desktop {

    namespace {

        std::vector<std::string_view> split(std::string_view text, char separator) {
            std::vector<std::string_view> parts;
            while (true) {
                const auto at = text.find(separator);
                parts.push_back(text.substr(0, at));
                if (at == std::string_view::npos)
                    return parts;
                text = text.substr(at + 1);
            }
        }

        // os-release values are shell words: quoted with " or ', or bare.
        std::string unquote(std::string_view value) {
            if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
                value = value.substr(1, value.size() - 2);
            std::string result;
            for (std::size_t index = 0; index < value.size(); ++index) {
                if (value[index] == '\\' && index + 1 < value.size())
                    ++index;
                result += value[index];
            }
            return result;
        }

        struct PatternDeleter {
            void operator()(FcPattern* pattern) const { FcPatternDestroy(pattern); }
        };
        struct ObjectSetDeleter {
            void operator()(FcObjectSet* set) const { FcObjectSetDestroy(set); }
        };
        struct FontSetDeleter {
            void operator()(FcFontSet* set) const { FcFontSetDestroy(set); }
        };

    }

    std::optional<std::filesystem::path> findProgram(std::string_view name, std::string_view path) {
        for (const auto folder : split(path, ':')) {
            if (folder.empty())
                continue;
            const auto      file = std::filesystem::path{folder} / name;
            std::error_code error;
            if (std::filesystem::is_regular_file(file, error) && ::access(file.c_str(), X_OK) == 0)
                return file;
        }
        return std::nullopt;
    }

    bool hasFontFamily(const std::string& family) {
        // Loads fontconfig on the first call, and on later ones rescans the folders if a
        // font was added since.
        if (FcInitBringUptoDate() == FcFalse)
            return false;
        const std::unique_ptr<FcPattern, PatternDeleter> pattern{FcPatternCreate()};
        FcPatternAddString(pattern.get(), FC_FAMILY, reinterpret_cast<const FcChar8*>(family.c_str())); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): fontconfig's string type
        const std::unique_ptr<FcObjectSet, ObjectSetDeleter> objects{FcObjectSetBuild(FC_FAMILY, nullptr)};
        const std::unique_ptr<FcFontSet, FontSetDeleter>     fonts{FcFontList(nullptr, pattern.get(), objects.get())};
        return fonts != nullptr && fonts->nfont > 0;
    }

    std::vector<std::string> monospaceFamilies() {
        if (FcInitBringUptoDate() == FcFalse)
            return {};
        const std::unique_ptr<FcPattern, PatternDeleter> pattern{FcPatternCreate()};
        FcPatternAddInteger(pattern.get(), FC_SPACING, FC_MONO);
        const std::unique_ptr<FcObjectSet, ObjectSetDeleter> objects{FcObjectSetBuild(FC_FAMILY, nullptr)};
        const std::unique_ptr<FcFontSet, FontSetDeleter>     fonts{FcFontList(nullptr, pattern.get(), objects.get())};
        std::vector<std::string>                             families;
        if (fonts == nullptr)
            return families;
        for (int index = 0; index < fonts->nfont; ++index) {
            FcChar8* family = nullptr;
            if (FcPatternGetString(fonts->fonts[index], FC_FAMILY, 0, &family) != FcResultMatch) // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): fontconfig's array
                continue;
            std::string name{reinterpret_cast<const char*>(family)}; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): fontconfig's string type
            if (name.contains("Symbols") || std::ranges::contains(families, name))
                continue;
            families.push_back(std::move(name));
        }
        std::ranges::sort(families);
        return families;
    }

    bool Distribution::arch() const {
        return id == "arch" || std::ranges::contains(like, std::string_view{"arch"});
    }

    Distribution parseOsRelease(std::string_view text) {
        Distribution distribution;
        for (const auto line : split(text, '\n')) {
            const auto equals = line.find('=');
            if (line.empty() || line.front() == '#' || equals == std::string_view::npos)
                continue;
            const auto key   = line.substr(0, equals);
            auto       value = unquote(line.substr(equals + 1));
            if (key == "NAME")
                distribution.name = std::move(value);
            else if (key == "ID")
                distribution.id = std::move(value);
            else if (key == "ID_LIKE") {
                for (const auto word : split(value, ' '))
                    if (!word.empty())
                        distribution.like.emplace_back(word);
            }
        }
        return distribution;
    }

    Distribution currentDistribution() {
        for (const auto* file : {"/etc/os-release", "/usr/lib/os-release"}) {
            std::ifstream stream{file};
            if (!stream)
                continue;
            std::stringstream text;
            text << stream.rdbuf();
            return parseOsRelease(text.str());
        }
        return {};
    }

}
