#pragma once

// What the grid window shows and edits: its canvas and ImGui panels both
// read and change it, and neither keeps its own copy. No DGL or ImGui types
// here (decisions.md, "UI": only the drawing layer depends on the toolkit;
// CMake keeps their headers off umpire-app's include path).
//
// For now it is the spike's note grid, in sixteenths: a clip's window shows
// the clip's notes in it (showClip), and edits stay in the window. Milestone
// 10 makes the grid edit the clip itself (UMP in ticks); the selection stays
// here.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "App/Clip.h"

namespace umpire {

struct GridNote {
    int pitch    = 60;    // a row of the grid: an EDO step (rowSemitones())
    int start    = 0;     // in steps (sixteenths)
    int length   = 2;     // in steps
    int velocity = 100;   // 1..127
};

class GridModel {
public:
    static constexpr int kMinVelocity = 1, kMaxVelocity = 127;

    std::string           clipName = "Untitled clip";
    // The grid's tuning, as StageHand's sequences (its docs/sequences.md, "EDO
    // pitch"): each row is a step of `edo` equal divisions of the octave, and
    // the reference row sounds `referenceNote`. The rows cover the MIDI range,
    // so at 12-EDO row n is note n.
    int edo           = 12;
    int referenceNote = 60;
    // How the shown clip's scale came about (Clip::scaleFit), for the panel.
    ScaleFit         scaleFit = ScaleFit::Default;
    std::vector<int> scaleCandidates;
    // Set: the panel's tuning is the clip's to change (a command), and this
    // is called with it when the user lets go. Unset: the window's own.
    std::function<void(int edo, int referenceNote)> chooseScale;
    std::vector<GridNote> notes;
    int                   selected = -1;   // index into notes, or -1
    int                   steps    = 64;   // four bars of sixteenths

    // Changes since the start: a view that drew `version()` needn't draw again.
    std::uint64_t version() const noexcept { return changes; }
    void          touch() noexcept { ++changes; }

    int    rows() const noexcept { return 128 * edo / 12; }
    int    referenceRow() const noexcept { return referenceNote * edo / 12; }
    double rowSemitones(int row) const noexcept { return referenceNote + (row - referenceRow()) * 12.0 / edo; }
    // The row nearest a pitch in 7.9.
    int    rowFor(std::uint16_t pitch79) const noexcept;
    // A new tuning: the clip shown (if any, unedited) is laid out again in it;
    // edited notes keep their pitch on the nearest row.
    void   setTuning(int edo, int referenceNote);
    // Whether the notes are still the shown clip's, unedited.
    bool   showingClip() const noexcept { return shown != nullptr; }

    GridNote*       selectedNote() noexcept;
    const GridNote* selectedNote() const noexcept;

    // The topmost note covering `step` in row `pitch`, or -1.
    int noteAt(int pitch, int step) const noexcept;

    // Adds a note at `pitch` and `step`, kept inside the clip, and selects it.
    // Returns its index, or -1 if `step` is past the clip's end.
    int  addNote(int pitch, int step, int length = 2, int velocity = 100);
    void select(int index) noexcept;
    void deleteSelected();
    void clear();
    void newClip();
    // The clip's notes in its scale, each to the nearest sixteenth and the
    // nearest row (its pitch is its 7.9: notesOf()), and its name; the grid as
    // long as the clip, in whole bars. Edits after are the window's, not the
    // clip's.
    void showClip(std::shared_ptr<const Clip> clip);

    // The selected note, kept inside the grid. Each does nothing without one.
    void moveSelected(int start, int pitch);
    void transposeSelected(int by);
    void setSelectedVelocity(int velocity);
    void setSelectedLength(int length);

private:
    void layOut();   // the shown clip's notes, in the current tuning
    void edited() {
        shown.reset();
        touch();
    }

    std::shared_ptr<const Clip> shown;   // the clip, until the notes are edited
    std::uint64_t changes = 0;
};

} // namespace umpire
