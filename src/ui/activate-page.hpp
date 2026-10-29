// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "activation/effects.hpp"
#include "activation/record.hpp"

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    // Activate and Deactivate: the effects in Lantern's effects file, applied to the
    // running session in the areas the user allowed, and taken back in one click. Nothing
    // is written to the user's config.
    class ActivatePage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        ActivatePage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Draws the page again, after Lantern's colors or text size changed.
        void restyle();

      private:
        void showOverview();
        // Reads the effects file again, making it from the template the first time.
        void readEffects();
        void activate();
        void deactivate();
        void allow(activation::Area area, bool allowed);
        [[nodiscard]] bool allowed(activation::Area area) const;

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> body;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    footer;

        activation::Effects                                                  effects;
        // The user's own keybindings that an effect would bind again.
        std::vector<std::string>                                             collisions;
        // What is active now, if anything.
        std::optional<activation::Record>                                    active;
        std::vector<activation::Area>                                        allowedAreas;
        std::vector<std::string>                                             problems;
        std::string                                                          outcome;
    };

}
