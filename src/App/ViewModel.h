#pragma once

// What the main window shows and edits: the canvas and the ImGui panels both
// read and change it, and neither keeps its own copy. No DGL or ImGui types
// here (decisions.md, "UI": only the drawing layer depends on the toolkit;
// CMake keeps their headers off umpire-app's include path).
//
// For now it is the spike's note grid: one clip of notes in sixteenths.
// Milestone 5's clip model (UMP in ticks, with room for per-note data)
// replaces the notes; the window's layout and selection stay here.

#include <cstdint>
#include <string>
#include <vector>

namespace umpire {

struct GridNote {
    int pitch    = 60;    // a row of the grid (an EDO step)
    int start    = 0;     // in steps (sixteenths)
    int length   = 2;     // in steps
    int velocity = 100;   // 1..127
};

class ViewModel {
public:
    static constexpr int kPitches = 128;
    static constexpr int kMinVelocity = 1, kMaxVelocity = 127;

    std::string           clipName = "Untitled clip";
    std::vector<GridNote> notes;
    int                   selected = -1;   // index into notes, or -1
    int                   steps    = 64;   // four bars of sixteenths

    // Changes since the start: a view that drew `version()` needn't draw again.
    std::uint64_t version() const noexcept { return changes; }
    void          touch() noexcept { ++changes; }

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

    // The selected note, kept inside the grid. Each does nothing without one.
    void moveSelected(int start, int pitch);
    void transposeSelected(int by);
    void setSelectedVelocity(int velocity);
    void setSelectedLength(int length);

private:
    std::uint64_t changes = 0;
};

} // namespace umpire
