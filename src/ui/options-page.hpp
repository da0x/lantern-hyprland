// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/options.hpp"
#include "application/session.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/element/Textbox.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    // Every option Hyprland has, with what it does, its value and its default, kept
    // current by the session. Those the config sets stand out, and a search narrows the
    // list to what it names.
    class OptionsPage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        OptionsPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        void showConnection(application::Connection connection, const std::string& reason);
        void showOptions(const std::expected<application::Options, std::string>& options);
        // Draws the list again, after Lantern's colors changed: its cells carry colors.
        void restyle();

      private:
        // Which options the list shows.
        enum class Filter {
            everything,
            set,
            changed,
        };

        void updateStatus();
        void updateFilters();
        void showRows();
        void showDetails();

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         status;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextboxElement>      search;
        std::vector<std::pair<Filter, Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement>>> filterButtons;
        RowList                                                              list;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> details;

        std::vector<application::OptionRow> rows;
        // The rows the list shows, as indices into `rows`.
        std::vector<std::size_t>            shownRows;
        std::optional<std::string>          selected;
        Filter                              filter = Filter::everything;
        std::string                         query;
        application::Connection             connection = application::Connection::connecting;
        std::string                         connectionReason;
        std::string                         optionsError;
        std::string                         configurationProblem;
        bool                                received = false;
    };

}
