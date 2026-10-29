// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ui {

    // Settings drawn as rows of cards, each card a picture of what its choice does and its
    // name, the chosen one outlined in the accent.
    class ChoiceCards {
      public:
        struct Choice {
            std::string                                                            label;
            std::function<bool()>                                                  chosen;
            std::function<void()>                                                  select;
            // The picture for what is wanted now; see picture().
            std::function<Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement>()> draw;
        };

        // `changed` runs after a card is clicked and the cards are marked again. Pictures are
        // `pictureHeight` tall.
        ChoiceCards(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, std::function<void()> changed,
                    double pictureHeight = defaultPictureHeight);

        // A setting in `column`: its name, a few words on what it does, and its choices.
        void addSetting(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column, const std::string& name, const std::string& about,
                        std::vector<Choice> choices);

        // Forgets the cards, before the page is drawn again.
        void clear();
        // Marks the chosen cards and draws each picture for what is wanted now.
        void update();
        // Forgets the pictures too, after Lantern's colors or sizes changed.
        void forget();

        // A picture made once for each key and kept, so showing it again does not flicker
        // while its text is drawn. A key belongs to one card, since an element has one parent.
        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> picture(const std::string& key,
                                                                                       const std::function<Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement>()>& make);

        static constexpr double defaultPictureHeight = 52;

      private:
        struct Card {
            Choice                                                               choice;
            Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRectangleElement>    frame  = nullptr;
            Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> holder = nullptr;
            Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         name   = nullptr;
        };

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>                       palette;
        std::function<void(std::function<void()>)>                                     defer;
        std::function<void()>                                                          changed;
        double                                                                         pictureHeight;
        std::vector<Card>                                                              cards;
        std::map<std::string, Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement>> pictures;
    };

}
