// SPDX-License-Identifier: GPL-3.0-or-later
#include "foundation/environment.hpp"

#include <string_view>

extern char** environ; // NOLINT(readability-redundant-declaration): POSIX declares it nowhere else

namespace foundation {

    Environment currentEnvironment() {
        Environment environment;
        for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) { // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): environ is a C array
            const std::string_view text{*entry};
            const auto             equals = text.find('=');
            if (equals != std::string_view::npos)
                environment.emplace(text.substr(0, equals), text.substr(equals + 1));
        }
        return environment;
    }

    std::optional<std::string> variable(const Environment& environment, const std::string& name) {
        const auto found = environment.find(name);
        return found != environment.end() ? std::optional{found->second} : std::nullopt;
    }

}
