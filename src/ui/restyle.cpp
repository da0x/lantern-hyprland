// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/restyle.hpp"

#include <algorithm>
#include <vector>

namespace ui {

    namespace {

        // The UI runs on one thread, so this needs no lock.
        std::vector<std::function<bool(Restyle)>>& registered() {
            static std::vector<std::function<bool(Restyle)>> list;
            return list;
        }

        // Elements come and go as pages are drawn again, so the list is pruned when it has
        // doubled since the last time.
        std::size_t prunedAt = 64;

    }

    void registerRestyle(std::function<bool(Restyle)> again) {
        auto& list = registered();
        list.push_back(std::move(again));
        if (list.size() < prunedAt * 2)
            return;
        std::erase_if(list, [](const auto& entry) { return !entry(Restyle::check); });
        prunedAt = std::max<std::size_t>(64, list.size());
    }

    std::size_t restyleCount() {
        return registered().size();
    }

    void restyleAll(bool resized, std::size_t upTo) {
        auto& list  = registered();
        auto  first = list.begin();
        auto  last  = list.begin() + static_cast<std::ptrdiff_t>(std::min(upTo, list.size()));
        list.erase(std::remove_if(first, last, [resized](const auto& entry) { return !entry(resized ? Restyle::resize : Restyle::recolor); }), last);
    }

}
