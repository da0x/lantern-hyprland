// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/program-switch.hpp"

#include "configuration/file-change.hpp"
#include "desktop/system.hpp"
#include "foundation/environment.hpp"
#include "ui/restyle.hpp"
#include "ui/texts.hpp"
#include "ui/widgets.hpp"

#include <format>

namespace ui {

    using Hyprtoolkit::CDynamicSize;
    using Hyprutils::Memory::CSharedPointer;

    ProgramSwitch::ProgramSwitch(CSharedPointer<Hyprtoolkit::CPalette> palette, std::function<void(std::function<void()>)> defer, application::EventLoop loop, std::string program,
                                 std::string title, std::function<void()> changed) :
        palette{std::move(palette)}, defer{std::move(defer)}, loop{std::move(loop)}, name{std::move(program)}, title{std::move(title)}, changed{std::move(changed)} {
        area  = Hyprtoolkit::CColumnLayoutBuilder::begin()->gap(4)->size(CDynamicSize{CDynamicSize::HT_SIZE_PERCENT, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})->commence();
        state = Hyprtoolkit::CTextBuilder::begin()
                    ->text(" ")
                    ->color([palette = this->palette] { return palette->m_colors.text; })
                    ->align(Hyprtoolkit::HT_FONT_ALIGN_LEFT)
                    ->size(CDynamicSize{CDynamicSize::HT_SIZE_ABSOLUTE, CDynamicSize::HT_SIZE_AUTO, {1.0, 1.0}})
                    ->commence();
        state->setGrow(true, false);
        restyled(state);
        onButton  = button("On", 70, false, [this] { this->defer([this] { if (this->program) this->program->turnOn(); }); });
        offButton = button("Off", 70, false, [this] { this->defer([this] { if (this->program) this->program->turnOff(); }); });
        refresh();
    }

    void ProgramSwitch::refresh() {
        const auto found = desktop::findProgram(name, foundation::variable(foundation::currentEnvironment(), "PATH").value_or(""));
        if (found != path) {
            path = found;
            program.reset();
            if (found)
                program.emplace(*found, loop, [this] { programChanged(); });
        } else if (program) {
            program->refresh();
        }
        update();
    }

    void ProgramSwitch::close() {
        program.reset();
    }

    bool ProgramSwitch::running() const {
        return program && program->running();
    }

    void ProgramSwitch::restartToRead(const std::filesystem::path& record, std::function<void(std::string)> done) {
        if (!running()) {
            done(std::format("Written. {} is off, so it was not restarted; it reads the new files when it starts.", title));
            return;
        }
        checking = record;
        checked  = std::move(done);
        program->restart();
    }

    void ProgramSwitch::restart() {
        if (program)
            program->restart();
    }

    void ProgramSwitch::turnOn() {
        if (program)
            program->turnOn();
    }

    void ProgramSwitch::turnOff() {
        if (program)
            program->turnOff();
    }

    void ProgramSwitch::programChanged() {
        // A change was just written and the program restarted with it: keep it if the
        // program runs, put the files back if it does not.
        if (checking && program && !program->busy()) {
            const auto record = *checking;
            checking.reset();
            std::string outcome;
            if (program->running()) {
                outcome = std::format("Written, and {} restarted with it.", title);
            } else if (const auto undone = configuration::undoChange(record); undone) {
                outcome = std::format("{} did not start with the new files, so they were put back as they were.", title);
                program->turnOn();
            } else {
                outcome = std::format("{} did not start with the new files, and they could not be put back: {}", title, undone.error());
            }
            if (checked)
                defer([done = std::exchange(checked, {}), outcome] { done(outcome); });
        }
        defer([this] {
            update();
            if (changed)
                changed();
        });
    }

    void ProgramSwitch::update() {
        area->clearChildren();
        if (!program)
            return;
        std::string shown;
        if (program->busy())
            shown = program->running() ? "stopping…" : "starting…";
        else
            shown = program->running() ? "running" : "not running";
        state->rebuild()->text(std::format("<span weight=\"600\">{}</span>   {}", title, shown))->commence();
        onButton->rebuild()->accent(program->running())->commence();
        offButton->rebuild()->accent(!program->running())->commence();

        const auto row = leftRow();
        row->addChild(state);
        row->addChild(onButton);
        row->addChild(offButton);
        area->addChild(row);
        if (program->error())
            area->addChild(paragraph(*program->error(), palette, true));
    }

}
