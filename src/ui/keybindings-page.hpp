// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/keybindings.hpp"
#include "application/session.hpp"
#include "ui/row-list.hpp"
#include "ui/source-view.hpp"

#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/element/Textbox.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ui {

    // Every keybinding Hyprland has right now, what each one does and where the config set
    // it, kept current by the session. It switches to the config's own source, where
    // clicking a binding opens the line that made it.
    class KeybindingsPage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        KeybindingsPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        void showConnection(application::Connection connection, const std::string& reason);
        void showKeybindings(const std::expected<application::Keybindings, std::string>& keybindings);

        // Draws the bindings again, after Lantern's colors changed: their keys and
        // actions carry colors in their text.
        void restyle();

      private:
        void                                                                 updateStatus();
        void                                                                 showList();
        // Opens one of the config's files, with `line` marked; 0 marks nothing.
        void                                                                 showSource(const std::filesystem::path& file, int line, bool jump);
        void                                                                 updateFileButtons();
        void                                                                 updateSwitch();
        [[nodiscard]] const application::SourceFile*                         source(const std::filesystem::path& file) const;

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         status;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         luaNote;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement>       listButton;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement>       sourceButton;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextboxElement>      search;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement>       clashButton;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    fileButtons;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         caption;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> body;
        RowList                                                              list;
        SourceView                                                           sourceView;

        // One binding in the list. A binding whose action is a function can be expanded to
        // the function's lines, each a row of its own.
        struct Entry {
            // Stays the same across reloads, so an expanded function stays expanded.
            std::string                                         identity;
            std::string                                         keys;
            std::string                                         action;
            std::string                                         notes;
            std::optional<configuration::Location>              location;
            // The function's lines after the first, with where each is.
            std::vector<std::pair<std::string, configuration::Location>> body;
            // The first line again, for when the function is expanded.
            std::string                                         expandedAction;
            // The entry's words without markup, for the search.
            std::string                                         searchable;
            // Other bindings for the same keys, triggered the same way.
            bool                                                clashes = false;
        };

        // What clicking a row does: expand or fold an entry, or open a line of the config.
        struct Target {
            std::optional<std::size_t>             toggles;
            std::optional<configuration::Location> opens;
        };

        void                                                                 showEntries();
        [[nodiscard]] Entry                                                  entry(std::string keys, const configuration::Value& action, std::string described,
                                                                                   const std::optional<configuration::Location>& location, std::string notes,
                                                                                   std::string searchable) const;
        void                                                                 updateClashButton();

        std::vector<Entry>                                                   entries;
        std::optional<std::expected<application::Keybindings, std::string>> shown;
        std::vector<Target>                                                  targets;
        std::set<std::string>                                                expanded;
        std::vector<application::SourceFile>                                 sources;

        // The file the source view shows, when it is the one showing.
        std::optional<std::filesystem::path>                                 openFile;
        int                                                                  markedLine = 0;

        application::Connection                                              connection = application::Connection::connecting;
        std::string                                                          connectionReason;
        std::string                                                          bindingsError;
        std::string                                                          configurationProblem;
        std::vector<std::string>                                             configurationDoubts;
        std::size_t                                                          count    = 0;
        std::size_t                                                          inactive = 0;
        // Keys bound more than once the same way.
        std::size_t                                                          clashing = 0;
        std::string                                                          query;
        bool                                                                 onlyClashes = false;
    };

}
