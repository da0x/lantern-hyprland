// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "application/outside-program.hpp"
#include "application/session.hpp"

#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/palette/Palette.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace ui {

    // An outside program's name, whether it runs, and On and Off for it. Off stops it,
    // whoever started it; neither changes what starts at login. Shows nothing while the
    // program is not installed.
    class ProgramSwitch {
      public:
        // `program` is the program's file name, such as waybar, and `title` its name as
        // people write it. `changed` runs whenever what the switch shows may have changed.
        ProgramSwitch(Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop,
                      std::string program, std::string title, std::function<void()> changed);

        [[nodiscard]] Hyprutils::Memory::CSharedPointer<Hyprtoolkit::IElement> element() const { return area; }

        // Looks for the program on the PATH and at what runs, as after an install.
        void                                                                   refresh();
        // Stops watching. Called before the event loop goes away.
        void                                                                   close();

        [[nodiscard]] bool                                                     running() const;

        // After Lantern wrote the program's files: restarts it so it reads them, if it is
        // on. If it then does not start, `record` is undone and it starts again with the
        // files as they were. `done` is told what happened, in a sentence.
        void                                                                   restartToRead(const std::filesystem::path& record, std::function<void(std::string)> done);

        // Restarts it, if it is on, as after an undo.
        void                                                                   restart();

        // Starts it, as its On button does.
        void                                                                   turnOn();
        // Stops it, as its Off button does.
        void                                                                   turnOff();

      private:
        void                                                                   update();
        void                                                                   programChanged();

        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CPalette>             palette;
        std::function<void(std::function<void()>)>                           defer;
        application::EventLoop                                               loop;
        std::string                                                          name;
        std::string                                                          title;
        std::function<void()>                                                changed;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CColumnLayoutElement> area;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CTextElement>         state;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement>       onButton;
        Hyprutils::Memory::CSharedPointer<Hyprtoolkit::CButtonElement>       offButton;
        std::optional<std::filesystem::path>                                 path;
        std::optional<application::OutsideProgram>                           program;
        std::optional<std::filesystem::path>                                 checking;
        std::function<void(std::string)>                                     checked;
    };

}
