// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/typing.hpp"

#include <hyprutils/memory/WeakPtr.hpp>

namespace ui {

    namespace {

        // The UI runs on one thread, so this needs no lock.
        std::vector<Hyprutils::Memory::CWeakPointer<Hyprtoolkit::CTextboxElement>>& registered() {
            static std::vector<Hyprutils::Memory::CWeakPointer<Hyprtoolkit::CTextboxElement>> list;
            return list;
        }

    }

    void registerTextbox(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextboxElement>& box) {
        registered().emplace_back(box);
    }

    std::vector<std::string> textboxTexts() {
        std::vector<std::string> texts;
        for (const auto& box : registered())
            if (const auto held = box.lock())
                texts.emplace_back(held->currentText());
        return texts;
    }

}
