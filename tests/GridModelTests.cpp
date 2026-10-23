// The view model's edits (src/App/GridModel.h): silent, no window.

#include <cstdio>

#include "App/GridModel.h"

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

    std::printf("%s: %d failure%s\n", failures == 0 ? "OK" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
