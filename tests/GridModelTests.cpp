// The view model's edits (src/App/GridModel.h): silent, no window.

#include <cmath>
#include <cstdio>
#include <memory>

#include "App/GridModel.h"
#include "uw/ump/Ump.h"

namespace {

int failures = 0;

void check(const char* what, bool ok) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

} // namespace

int main() {
    using umpire::GridModel;

    {
        GridModel m;
        const auto v0 = m.version();
        const int  i  = m.addNote(67, 4);
        check("adding a note selects it", i == 0 && m.selected == 0 && m.version() > v0);
        check("noteAt finds it over its length", m.noteAt(67, 4) == 0 && m.noteAt(67, 5) == 0 && m.noteAt(67, 6) == -1);
        check("a note past the clip's end isn't added", m.addNote(60, m.steps) == -1 && m.notes.size() == 1);
        m.addNote(60, m.steps - 1, 4);
        check("a note at the end is kept inside the clip", m.notes[1].start == m.steps - 4);
        check("the topmost note wins", (m.addNote(67, 4), m.noteAt(67, 4) == 2));
    }
    {
        GridModel m;
        m.addNote(60, 0);
        m.moveSelected(-5, 200);
        check("moving keeps the note inside the grid", m.notes[0].start == 0 && m.notes[0].pitch == 127);
        m.transposeSelected(-2);
        check("transposing", m.notes[0].pitch == 125);
        m.setSelectedVelocity(0);
        check("velocity stays at least 1", m.notes[0].velocity == 1);
        const auto v = m.version();
        m.setSelectedVelocity(1);
        check("an edit that changes nothing isn't a change", m.version() == v);
        m.setSelectedLength(1000);
        check("length stops at the clip's end", m.notes[0].length == m.steps);
    }
    {
        GridModel m;
        m.addNote(60, 0);
        m.select(5);
        check("selecting nothing that's there clears the selection", m.selected == -1 && m.selectedNote() == nullptr);
        m.transposeSelected(1);
        m.deleteSelected();
        check("edits without a selection do nothing", m.notes.size() == 1 && m.notes[0].pitch == 60);
        m.select(0);
        m.deleteSelected();
        check("deleting the selected note", m.notes.empty() && m.selected == -1);
        m.clipName = "Take";
        m.addNote(60, 0);
        m.newClip();
        check("a new clip is empty and untitled", m.notes.empty() && m.clipName == "Untitled clip");
    }

    {
        // A 31-EDO take, as KeyEDO sends it: each note's pitch in its Pitch 7.9
        // attribute, on the nearest note number.
        using namespace umpire;
        const auto step31 = [](int s) { return static_cast<std::uint16_t>(std::lround((60 + s * 12.0 / 31) * 512)); };
        auto clip    = std::make_shared<Clip>();
        clip->name   = "31";
        clip->length = kTicksPerQuarter * 4;
        for (int s : {0, 1, 2, 31}) {
            const std::uint16_t p = step31(s);
            const Tick          t = kTicksPerQuarter * (s % 4);
            clip->events.push_back({t, uw::ump::midi2NoteOn(0, 0, static_cast<std::uint8_t>(p >> 9), 0x8000, p)});
            clip->events.push_back({t + kTicksPerQuarter / 2, uw::ump::midi2NoteOff(0, 0, static_cast<std::uint8_t>(p >> 9))});
        }
        GridModel m;
        m.showClip(clip);
        check("at 12-EDO, 31-EDO steps 0, 1 and 2 crowd onto two rows",
              m.notes[0].pitch == 60 && m.notes[1].pitch == 60 && m.notes[2].pitch == 61);
        m.setTuning(31, 60);
        const int ref = m.referenceRow();
        check("at 31-EDO each step has its row, an octave 31 rows up",
              m.rows() == 330 && m.notes[0].pitch == ref && m.notes[1].pitch == ref + 1 && m.notes[2].pitch == ref + 2 &&
                  m.notes[3].pitch == ref + 31 && std::abs(m.rowSemitones(ref + 31) - 72) < 1e-9);
        m.select(1);
        m.transposeSelected(1);
        m.setTuning(12, 60);
        check("edited, a new tuning keeps each note's pitch on the nearest row",
              m.notes[1].pitch == 61 && m.notes[0].pitch == 60 && m.notes[3].pitch == 72);
        m.setTuning(1000, 200);
        check("the tuning stays in range", m.edo == Scale::kMaxEdo && m.referenceNote == 127);
    }
    {
        // A note's pitch: its 7.9 attribute; else the key's Pitch 7.25, until a reset; else its number.
        using namespace umpire;
        const uw::MidiMessage tune{{0x40003D03u, 0x7A800000u, 0, 0}, 2};   // key 61 to 61.25
        const uw::MidiMessage reset{{0x40F03D01u, 0, 0, 0}, 2};
        const std::vector<ClipEvent> events{{0, tune},
                                            {1, uw::ump::midi2NoteOn(0, 0, 61, 0x8000)},
                                            {2, uw::ump::midi2NoteOn(0, 0, 62, 0x8000, 62 * 512 + 100)},
                                            {3, reset},
                                            {4, uw::ump::midi2NoteOn(0, 0, 61, 0x8000)}};
        const auto notes = notesOf(events, 10);
        check("pitch from Pitch 7.25, from the 7.9 attribute, and back to the note number after a reset",
              notes.size() == 3 && notes[0].pitch79 == 61 * 512 + 128 && notes[1].pitch79 == 62 * 512 + 100 &&
                  notes[2].pitch79 == 61 * 512);
    }

    std::printf("%s: %d failure%s\n", failures == 0 ? "OK" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
