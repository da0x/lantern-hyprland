// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/choice-cards.hpp"

#include "ui/markup.hpp"
#include "ui/widgets.hpp"

#include <format>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprtoolkit::CFontSize;
        using Hyprutils::Memory::CSharedPointer;

        // A card: its picture, its name, and the margins and gap around them.
        double cardHeight(double pictureHeight) {
            return pictureHeight + 2 * 6 + 4 + scaled(18);
        }

    }

    ChoiceCards::ChoiceCards(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, std::function<void()> changed, double pictureHeight) :
        palette{std::move(palette)}, defer{std::move(defer)}, changed{std::move(changed)}, pictureHeight{pictureHeight} {}

    void ChoiceCards::addSetting(const CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column, const std::string& name, const std::string& about, std::vector<Choice> choices) {
        column->addChild(markupLine(std::format("<span weight=\"600\">{}</span>  <span alpha=\"60%\" size=\"small\">{}</span>", escapeMarkup(name), escapeMarkup(about)), palette,
                                    CFontSize{CFontSize::HT_FONT_TEXT}, 1.0F));
        const auto row = leftRow(0);
        row->rebuild()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, cardHeight(pictureHeight)}})->gap(8)->commence();
        const auto count = static_cast<double>(choices.size());
        for (auto& choice : choices) {
            Card card{.choice = std::move(choice)};
            card.frame = Hyprtoolkit::CRectangleBuilder::begin()
                             ->color([palette = palette] { return palette->m_colors.alternateBase; })
                             ->rounding(palette->m_vars.bigRounding)
                             ->borderColor([] { return Hyprtoolkit::CHyprColor{0, 0, 0, 0}; })
                             ->borderThickness(2)
                             ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {(1.0 - 0.015 * (count - 1)) / count, cardHeight(pictureHeight)}})
                             ->commence();
            const auto inside = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(4)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_PERCENT, {1.0, 1.0}})->commence();
            inside->setMargin(6);
            card.holder = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, pictureHeight}})->commence();
            card.name   = markupLine(escapeMarkup(card.choice.label), palette, CFontSize{CFontSize::HT_FONT_SMALL});
            inside->addChild(card.holder);
            inside->addChild(card.name);
            card.frame->addChild(inside);
            card.frame->setReceivesMouse(true);
            card.frame->setMouseButton([this, position = cards.size()](Hyprtoolkit::Input::eMouseButton pressed, bool down) {
                if (pressed != Hyprtoolkit::Input::MOUSE_BUTTON_LEFT || !down)
                    return;
                defer([this, position] {
                    if (position >= cards.size())
                        return;
                    cards[position].choice.select();
                    update();
                    if (changed)
                        changed();
                });
            });
            row->addChild(card.frame);
            cards.push_back(std::move(card));
        }
        column->addChild(row);
        column->addChild(space(10));
    }

    void ChoiceCards::clear() {
        cards.clear();
    }

    void ChoiceCards::forget() {
        cards.clear();
        pictures.clear();
    }

    void ChoiceCards::update() {
        for (auto& card : cards) {
            const bool chosen = card.choice.chosen();
            card.frame->rebuild()->borderColor([palette = palette, chosen] { return chosen ? palette->m_colors.accent : Hyprtoolkit::CHyprColor{0, 0, 0, 0}; })->commence();
            card.name->rebuild()->text(chosen ? std::format("<span weight=\"600\">{}</span>", escapeMarkup(card.choice.label)) : escapeMarkup(card.choice.label))->commence();
            card.holder->clearChildren();
            card.holder->addChild(card.choice.draw());
        }
    }

    CSharedPointer<Hyprtoolkit::IElement> ChoiceCards::picture(const std::string& key, const std::function<CSharedPointer<Hyprtoolkit::IElement>()>& make) {
        auto& made = pictures[key];
        if (!made)
            made = make();
        return made;
    }

}
