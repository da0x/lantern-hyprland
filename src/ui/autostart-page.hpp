// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/session.hpp"
#include "configuration/autostart.hpp"
#include "configuration/file-change.hpp"
#include "desktop/autostart-entries.hpp"
#include "ui/row-list.hpp"

#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/Textbox.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    // What starts when you log in: the programs Hyprland's config starts, and the
    // autostart files that ask to start with any desktop, each switched on or off, and a
    // program added. Written only after the user reviews the change.
    class AutostartPage {
      public:
        // `defer` runs a function after the current layout pass; see RowList.
        AutostartPage(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return root; }

        // Reads the files again, when the page is opened.
        void refresh();

        // Draws the page again, after Lantern's colors or text size changed.
        void restyle();

      private:
        void showOverview();
        void fillHyprland(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column);
        void fillEntries(const Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement>& column);
        void showReview();
        void apply();
        void undo();
        void fillStatus();
        // Adds what is typed, or switches it on when the config already has it.
        void add(std::string command);
        [[nodiscard]] std::expected<std::vector<configuration::FileChange>, std::string> plan() const;

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        application::EventLoop                                               loop;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> root;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> body;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    footer;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CRowLayoutElement>    adding;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextboxElement>      typed;
        RowList                                                              diff;

        std::optional<std::string>                                           configProblem;
        std::vector<configuration::StartedProgram>                           startedNow;
        std::vector<configuration::StartedProgram>                           startedWanted;
        std::vector<desktop::AutostartEntry>                                 entries;
        // By id, the entries the user switched.
        std::map<std::string, bool>                                          entriesWanted;
        // Whether systemd starts the autostart files, as it does in a session uwsm runs;
        // nothing until systemctl answers.
        std::optional<bool>                                                  systemdStarts;
        bool                                                                 reviewing = false;
        std::vector<configuration::FileChange>                               changes;
        std::string                                                          outcome;
    };

}
