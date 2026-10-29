// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets.hpp"

#include "configuration/diff.hpp"
#include "desktop/clipboard.hpp"
#include "ui/markup.hpp"
#include "ui/restyle.hpp"
#include "ui/texts.hpp"

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Combobox.hpp>
#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/Slider.hpp>

#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <format>

namespace ui {

    using Hyprtoolkit::CDynamicSize;
    using Hyprtoolkit::CFontSize;
    using Hyprutils::Memory::CSharedPointer;

    namespace {

        // A slider that shows its number: hyprtoolkit 0.6.0 always does, and later versions
        // only when asked, with a showLabel that 0.6.0 does not have.
        template <typename Builder>
        Builder withLabel(Builder builder) {
            if constexpr (requires { builder->showLabel(true); })
                builder->showLabel(true);
            return builder;
        }

        // A command to type, in the monospace font on a panel of its own.
        CSharedPointer<Hyprtoolkit::IElement> commandPanel(const std::string& command, const CSharedPointer<Hyprtoolkit::CPalette>& palette) {
            const auto panel = Hyprtoolkit::CRectangleBuilder::begin()
                                   ->color([palette] { return palette->m_colors.alternateBase; })
                                   ->rounding(palette->m_vars.smallRounding)
                                   ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 40.0}})
                                   ->commence();
            // Takes the row's width left over by the Copy button.
            panel->setGrow(true, false);
            const auto text = Hyprtoolkit::CTextBuilder::begin()
                                  ->text(std::format("<span font_family=\"{}\">{}</span>", escapeMarkup(palette->m_vars.fontFamilyMonospace), escapeMarkup(command)))
                                  ->color([palette] { return palette->m_colors.text; })
                                  ->align(Hyprtoolkit::HT_FONT_ALIGN_LEFT)
                                  ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})
                                  ->commence();
            text->setMargin(12);
            panel->addChild(text);
            restyled(panel);
            restyled(text);
            return panel;
        }

    }

    // A line of markup that wraps, as wide as its column.
    CSharedPointer<Hyprtoolkit::CTextElement> markupLine(const std::string& markup, const CSharedPointer<Hyprtoolkit::CPalette>& palette, CFontSize size, float alpha) {
        const auto made = Hyprtoolkit::CTextBuilder::begin()
            ->text(std::string{markup})
            ->fontSize(CFontSize{size})
            ->color([palette] { return palette->m_colors.text; })
            ->a(alpha)
            ->noEllipsize(true)
            ->align(Hyprtoolkit::HT_FONT_ALIGN_LEFT)
            ->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})
            ->commence();
        restyled(made, size);
        return made;
    }

    double scaled(double pixels) {
        // hyprtoolkit's own default text size, in points.
        constexpr double designed = 11;
        return pixels * std::max(1.0, static_cast<double>(CFontSize{CFontSize::HT_FONT_TEXT}.ptSize()) / designed);
    }

    void scrollWhenLaidOut(const CSharedPointer<Hyprtoolkit::CScrollAreaElement>& area, Hyprutils::Math::Vector2D offset, const std::function<void(std::function<void()>)>& defer) {
        // Once: after the first pass the area keeps its scroll, and the user's.
        auto pending = std::make_shared<bool>(true);
        area->setRepositioned([weak = Hyprutils::Memory::CWeakPointer<Hyprtoolkit::CScrollAreaElement>{area}, offset, defer, pending] {
            if (!*pending)
                return;
            *pending = false;
            // Not from inside the layout pass that called this.
            defer([weak, offset] {
                if (const auto alive = weak.lock())
                    alive->setScroll(offset);
            });
        });
    }

    CSharedPointer<Hyprtoolkit::IElement> space(double height) {
        return Hyprtoolkit::CNullBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, height}})->commence();
    }

    // A path under the home folder with ~ for the home folder.
    std::string shortPath(const std::filesystem::path& path) {
        const auto* home = std::getenv("HOME"); // NOLINT(concurrency-mt-unsafe): read once, on the UI thread
        if (home == nullptr)
            return path.string();
        const auto relative = path.lexically_relative(home);
        if (relative.empty() || relative.native().starts_with(".."))
            return path.string();
        return (std::filesystem::path{"~"} / relative).string();
    }

    // The command's panel, with a button that copies it when wl-copy is installed.
    CSharedPointer<Hyprtoolkit::IElement> commandRow(const std::string& command, const std::optional<std::filesystem::path>& copier,
                                                     const CSharedPointer<Hyprtoolkit::CPalette>& palette, const std::function<void(std::function<void()>)>& defer,
                                                     const std::string& lead) {
        const auto row = Hyprtoolkit::CRowLayoutBuilder::begin()->gap(8)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, 40.0}})->commence();
        if (!lead.empty()) {
            const auto text = Hyprtoolkit::CTextBuilder::begin()
                                  ->text(std::string{lead})
                                  ->color([palette] { return palette->m_colors.text; })
                                  ->a(0.85F)
                                  ->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})
                                  ->commence();
            restyled(text);
            row->addChild(text);
        }
        row->addChild(commandPanel(command, palette));
        if (!copier)
            return row;
        const auto copy = Hyprtoolkit::CButtonBuilder::begin()
                          ->label("Copy")
                          ->noBorder(true)
                          ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {90.0, 40.0}})
                          ->onMainClick([command, program = *copier, defer](CSharedPointer<Hyprtoolkit::CButtonElement> button) {
                              const auto copied = desktop::copyText(program, command);
                              defer([copied, weak = Hyprutils::Memory::CWeakPointer<Hyprtoolkit::CButtonElement>{button}] {
                                  if (const auto shown = weak.lock())
                                      shown->rebuild()->label(copied ? "Copied" : "Not copied")->commence();
                              });
                          })
                          ->commence();
        restyled(copy);
        row->addChild(copy);
        return row;
    }

    CSharedPointer<Hyprtoolkit::IElement> requirementBlock(const desktop::Requirement& requirement, const desktop::Distribution& distribution,
                                                           const std::optional<std::filesystem::path>& copier, const CSharedPointer<Hyprtoolkit::CPalette>& palette,
                                                           const std::function<void(std::function<void()>)>& defer, std::function<void()> recheck,
                                                           const std::string& problem) {
        const bool dark  = darkTheme(*palette);
        const auto green = dark ? std::string{"#a6e3a1"} : std::string{"#2f7d32"};
        const auto red   = dark ? std::string{"#f38ba8"} : std::string{"#c62828"};

        const auto block = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(4)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        const auto amber = dark ? std::string{"#f9e2af"} : std::string{"#a15c00"};
        const auto mark  = !requirement.met() ? colored("✗", red) : problem.empty() ? colored("✓", green) : colored("!", amber);
        const auto state = requirement.met() ? escapeMarkup(*requirement.found) : colored("not installed", red);
        block->addChild(markupLine(std::format("{}  <span weight=\"600\">{}</span>   {}", mark, escapeMarkup(requirement.name), state), palette));
        block->addChild(paragraph(requirement.purpose, palette, true));
        block->addChild(markupLine(colored(requirement.upstream, hex(palette->m_colors.linkText)), palette, CFontSize{CFontSize::HT_FONT_SMALL}));
        // In a row of its own, which keeps it at the left: a column centers what is narrower.
        const auto again = [&defer, &recheck] {
            const auto row = leftRow(34);
            row->addChild(button("Check again", 140, false, [defer, recheck] { defer(recheck); }));
            return row;
        };
        if (requirement.met()) {
            if (!problem.empty()) {
                block->addChild(markupLine(colored(escapeMarkup(problem), amber), palette));
                block->addChild(again());
            }
            return block;
        }

        block->addChild(space(4));
        if (const auto command = desktop::installCommand(requirement, distribution); !command.empty()) {
            block->addChild(paragraph("Run this in a terminal, then press Check again:", palette));
            block->addChild(commandRow(command, copier, palette, defer));
            if (!copier)
                block->addChild(paragraph("A Copy button appears here when wl-clipboard is installed; Lantern copies through its wl-copy.", palette, true));
        } else {
            const auto where = distribution.name.empty() ? std::string{"your distribution"} : distribution.name;
            block->addChild(paragraph(std::format("Install it from {}'s packages, then press Check again. On Arch Linux the package is {}.", where, requirement.package), palette));
        }
        block->addChild(again());
        return block;
    }

    CSharedPointer<Hyprtoolkit::CButtonElement> button(std::string label, double width, bool accent, std::function<void()> clicked) {
        const auto made = Hyprtoolkit::CButtonBuilder::begin()
            ->label(std::move(label))
            ->noBorder(true)
            ->accent(accent)
            ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {scaled(width), scaled(34.0)}})
            ->onMainClick([clicked = std::move(clicked)](CSharedPointer<Hyprtoolkit::CButtonElement>) { clicked(); })
            ->commence();
        restyled(made);
        return made;
    }

    CSharedPointer<Hyprtoolkit::IElement> letterButton(const std::string& letter, float points, const CSharedPointer<Hyprtoolkit::CPalette>& palette,
                                                       const std::function<void(std::function<void()>)>& defer, std::function<void()> clicked) {
        const double     width  = scaled(40);
        const double     height = scaled(34);
        const auto       frame  = Hyprtoolkit::CRectangleBuilder::begin()
                               ->color([palette] { return palette->m_colors.alternateBase; })
                               ->rounding(palette->m_vars.smallRounding)
                               ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {width, height}})
                               ->commence();
        // A column centers the letter across; a spacer above, of half what the line leaves,
        // centers it up and down.
        const auto column = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {width, height}})->commence();
        const auto line   = static_cast<double>(points) * 4.0 / 3.0 * 1.2;
        column->addChild(Hyprtoolkit::CNullBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, std::max(0.0, (height - line) / 2)}})->commence());
        const auto text = Hyprtoolkit::CTextBuilder::begin()
                              ->text(std::string{letter})
                              ->fontSize(CFontSize{CFontSize::HT_FONT_ABSOLUTE, points})
                              ->color([palette] { return palette->m_colors.text; })
                              ->size(CDynamicSize{CDynamicSize::HT_SIZE_AUTO, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})
                              ->commence();
        column->addChild(text);
        frame->addChild(column);
        restyled(frame);
        restyled(text, CFontSize{CFontSize::HT_FONT_ABSOLUTE, points});
        frame->setReceivesMouse(true);
        frame->setMouseButton([defer, clicked = std::move(clicked)](Hyprtoolkit::Input::eMouseButton button, bool pressed) {
            if (button == Hyprtoolkit::Input::MOUSE_BUTTON_LEFT && pressed)
                defer(clicked);
        });
        return frame;
    }

    // A full-width row, so what is in it starts at the left edge rather than the column's middle.
    CSharedPointer<Hyprtoolkit::CRowLayoutElement> leftRow(double height) {
        return Hyprtoolkit::CRowLayoutBuilder::begin()->gap(8)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_ABSOLUTE, {1.0, scaled(height)}})->commence();
    }

    CSharedPointer<Hyprtoolkit::CRowLayoutElement> growingRow() {
        return Hyprtoolkit::CRowLayoutBuilder::begin()->gap(8)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
    }

    CSharedPointer<Hyprtoolkit::IElement> heading(const std::string& text, const CSharedPointer<Hyprtoolkit::CPalette>& palette) {
        return markupLine(std::format("<span weight=\"600\">{}</span>", escapeMarkup(text)), palette, CFontSize{CFontSize::HT_FONT_H3}, 1.0F);
    }

    // A diff as list rows: a header per file, then its changes with a sign and a color.
    std::vector<RowList::Row> diffRows(const std::vector<configuration::FileChange>& changes, const Hyprtoolkit::CPalette& palette) {
        using Kind                        = configuration::DiffLine::Kind;
        const bool                dark    = darkTheme(palette);
        const auto                added   = dark ? std::string{"#a6e3a1"} : std::string{"#2f7d32"};
        const auto                removed = dark ? std::string{"#f38ba8"} : std::string{"#c62828"};
        const auto                faded   = dark ? std::string{"#7d8799"} : std::string{"#6a737d"};
        std::vector<RowList::Row> rows;
        for (const auto& change : changes) {
            if (!change.changes())
                continue;
            const auto what = change.current ? "changed" : "a new file";
            rows.push_back({{"", std::format("<b>{}</b>   {}", escapeMarkup(shortPath(change.file)), what)}, true});
            for (const auto& line : configuration::changesOnly(configuration::diffLines(change.current.value_or(""), change.proposed))) {
                if (line.kind == Kind::same && line.line == 0) {
                    rows.push_back({{"", colored("…", faded)}, false});
                    continue;
                }
                const auto& color = line.kind == Kind::added ? added : line.kind == Kind::removed ? removed : faded;
                const auto* sign   = line.kind == Kind::added ? "+" : line.kind == Kind::removed ? "−" : "";
                // colored() escapes what it is given, so only the plain line is escaped here.
                const auto  text   = line.text.empty() ? std::string{" "} : line.text;
                rows.push_back({{colored(sign, color), line.kind == Kind::same ? escapeMarkup(text) : colored(text, color)}, false});
            }
        }
        return rows;
    }

    void setFooter(const CSharedPointer<Hyprtoolkit::CRowLayoutElement>& footer, const std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>>& buttons,
                   const CSharedPointer<Hyprtoolkit::IElement>& note, const std::vector<CSharedPointer<Hyprtoolkit::CButtonElement>>& away) {
        footer->clearChildren();
        for (const auto& made : away)
            footer->addChild(made);
        const auto spring = space(1);
        spring->setGrow(true, false);
        footer->addChild(spring);
        if (note) {
            footer->addChild(note);
            footer->addChild(Hyprtoolkit::CNullBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {16.0, 1.0}})->commence());
        }
        for (const auto& made : buttons)
            footer->addChild(made);
    }

    CSharedPointer<Hyprtoolkit::CRowLayoutElement> settingRow(const std::string& name, const std::string& about, std::vector<std::string> items, std::size_t current,
                                                              const CSharedPointer<Hyprtoolkit::CPalette>& palette, std::function<void(std::size_t)> changed,
                                                              const std::function<void(std::function<void()>)>& defer) {
        const auto row   = growingRow();
        const auto names = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {scaled(220.0), 1.0}})->commence();
        names->addChild(markupLine(std::format("<b>{}</b>", escapeMarkup(name)), palette));
        names->addChild(markupLine(escapeMarkup(about), palette, CFontSize{CFontSize::HT_FONT_SMALL}, 0.6F));
        row->addChild(names);
        row->addChild(Hyprtoolkit::CComboboxBuilder::begin()
                          ->items(std::move(items))
                          ->currentItem(current)
                          ->onChanged([changed = std::move(changed), defer](CSharedPointer<Hyprtoolkit::CComboboxElement>, std::size_t index) {
                              defer([changed, index] { changed(index); });
                          })
                          ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {scaled(260.0), scaled(34.0)}})
                          ->commence());
        return row;
    }

    CSharedPointer<Hyprtoolkit::CRowLayoutElement> sliderRow(const std::string& name, const std::string& about, int least, int most, int current,
                                                             const CSharedPointer<Hyprtoolkit::CPalette>& palette, std::function<void(int)> changed,
                                                             const std::function<void(std::function<void()>)>& defer) {
        const auto row   = growingRow();
        const auto names = Hyprtoolkit::CColumnLayoutBuilder::begin()->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {scaled(220.0), 1.0}})->commence();
        names->addChild(markupLine(std::format("<b>{}</b>", escapeMarkup(name)), palette));
        names->addChild(markupLine(escapeMarkup(about), palette, CFontSize{CFontSize::HT_FONT_SMALL}, 0.6F));
        row->addChild(names);
        row->addChild(withLabel(Hyprtoolkit::CSliderBuilder::begin())
                          ->min(static_cast<float>(least))
                          ->max(static_cast<float>(most))
                          ->val(static_cast<float>(current))
                          ->snapInt(true)
                          ->onChanged([changed = std::move(changed), defer, last = current](CSharedPointer<Hyprtoolkit::CSliderElement>, float value) mutable {
                              const auto whole = static_cast<int>(std::lround(value));
                              if (whole == last)
                                  return;
                              last = whole;
                              defer([changed, whole] { changed(whole); });
                          })
                          ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_ABSOLUTE, {scaled(260.0), scaled(34.0)}})
                          ->commence());
        return row;
    }

}
