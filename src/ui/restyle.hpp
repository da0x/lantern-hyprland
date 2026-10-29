// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/types/FontTypes.hpp>
#include <hyprutils/memory/SharedPtr.hpp>
#include <hyprutils/memory/WeakPtr.hpp>

#include <concepts>
#include <cstddef>
#include <functional>
#include <limits>

namespace ui {

    // hyprtoolkit takes an element's color when it is made or rebuilt, so after Lantern's
    // palette changes, every element made in the old colors is rebuilt. The
    // places that make long-lived elements register them here.

    // What a registered element is asked to do.
    enum class Restyle {
        // Only say whether it is still there.
        check,
        // Take the palette's colors.
        recolor,
        // Take the palette's colors and text sizes.
        resize,
    };

    // `again` says whether its element is still there, and does what it is asked.
    void registerRestyle(std::function<bool(Restyle)> again);

    // `size` is the element's own font size, for text and buttons. hyprtoolkit draws text
    // again only when the size it is given differs from the one it had, and works both out
    // from the palette as it is now; after the palette's sizes change they look the same to
    // it. So on a resize a text is given another size first, then its own.
    template <typename Element>
    void restyled(const Hyprutils::Memory::CSharedPointer<Element>& element, Hyprtoolkit::CFontSize size = Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_TEXT}) {
        registerRestyle([weak = Hyprutils::Memory::CWeakPointer<Element>{element}, size](Restyle asked) {
            const auto alive = weak.lock();
            if (!alive)
                return false;
            if constexpr (std::same_as<Element, Hyprtoolkit::CTextElement> || std::same_as<Element, Hyprtoolkit::CButtonElement>) {
                if (asked == Restyle::resize) {
                    alive->rebuild()->fontSize(Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_ABSOLUTE, 1.F})->commence();
                    alive->rebuild()->fontSize(Hyprtoolkit::CFontSize{size})->commence();
                    return true;
                }
            }
            if (asked != Restyle::check)
                alive->rebuild()->commence();
            return true;
        });
    }

    // How many elements are registered, for restyleAll's `upTo`.
    [[nodiscard]] std::size_t restyleCount();

    // Rebuilds every registered element that is still alive: in the palette's colors, and
    // with `resized`, its text sizes too. Only the first `upTo` are rebuilt: elements made
    // since are already in the new colors and sizes, and a text not laid out yet would be
    // drawn once at no width, a letter to a line, when given a new size.
    void restyleAll(bool resized = false, std::size_t upTo = std::numeric_limits<std::size_t>::max());

}
