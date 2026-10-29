// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/texts.hpp"

#include "ui/markup.hpp"
#include "ui/restyle.hpp"

#include <format>

namespace ui {

    namespace {

        using Hyprtoolkit::CDynamicSize;
        using Hyprtoolkit::CFontSize;

    }

    Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement> pageTitle(const std::string& text, const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>& palette) {
        const auto made = Hyprtoolkit::CTextBuilder::begin()
            ->text(std::format("<span weight=\"600\">{}</span>", escapeMarkup(text)))
            ->fontSize(CFontSize{CFontSize::HT_FONT_H1, 1.3F})
            ->color([palette] { return palette->m_colors.text; })
            ->align(Hyprtoolkit::HT_FONT_ALIGN_LEFT)
            ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})
            ->commence();
        restyled(made, CFontSize{CFontSize::HT_FONT_H1, 1.3F});
        return made;
    }

    Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement> paragraph(const std::string& text, const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>& palette, bool faded) {
        const auto made = Hyprtoolkit::CTextBuilder::begin()
            ->text(escapeMarkup(text.empty() ? std::string{" "} : text))
            ->fontSize(CFontSize{faded ? CFontSize::HT_FONT_SMALL : CFontSize::HT_FONT_TEXT})
            ->color([palette] { return palette->m_colors.text; })
            ->a(faded ? 0.6F : 0.85F)
            ->noEllipsize(true)
            ->align(Hyprtoolkit::HT_FONT_ALIGN_LEFT)
            ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})
            ->commence();
        restyled(made, CFontSize{faded ? CFontSize::HT_FONT_SMALL : CFontSize::HT_FONT_TEXT});
        return made;
    }

}
