#pragma once

// What the grid window shows and edits: its canvas and ImGui panels both
// read and change it, and neither keeps its own copy. No DGL or ImGui types
// here (decisions.md, "UI": only the drawing layer depends on the toolkit;
// CMake keeps their headers off umpire-app's include path).
//
// A clip's window shows the clip's notes (showClip) and edits the clip
// (milestone 10, docs/clip-editor.md): a gesture changes the notes here as it
// goes, a preview, and commit() then makes it one command on the clip through
// `editClip`, after which the grid shows the new clip. Without `editClip` (the
// tests, and milestone 4's grid) edits stay in the window. The selection is
// the window's, not the session's.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "App/Clip.h"
#include "App/ClipEdit.h"

namespace umpire {

inline constexpr Tick kGridStep = kTicksPerQuarter / 4;   // a sixteenth: the grid's columns

struct GridNote {
    int  pitch    = 60;    // a row of the grid: an EDO step (rowSemitones())
    Tick start    = 0;     // in ticks
    Tick length   = kGridStep * 2;
    int  velocity = 100;   // 1..127
    bool selected = false;

    // A note from the clip: what was sent, so the inspector can say (the
    // grid's rows and 7-bit velocity are only its view of it), and its
    // note-on, so an edit can name it.
    bool          fromClip   = false;
    std::size_t   on         = NoteSpan::npos;
    int           sentNote   = 60;        // its note number
    std::uint16_t pitch79    = 60 << 9;   // its pitch, as NoteSpan
    PitchFrom     pitchFrom  = PitchFrom::NoteNumber;
    std::uint16_t velocity16 = 0;
    int           clipRow    = 60;        // the row it was shown on: moved when pitch differs
};

class GridModel {
public:
    static constexpr int  kMinVelocity = 1, kMaxVelocity = 127;
    static constexpr Tick kStep = kGridStep;

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
    // Set: edits are the clip's. Called with what changed and the command's
    // name; returns the new clip (null if it didn't take), which the grid then
    // shows, with the changed notes selected.
    std::function<EditedClip(const std::string& label, const std::vector<NoteChange>&, const std::vector<NewNote>&)>
        editClip;
    // Set: the clip's name is the clip's (a command).
    std::function<void(const std::string& name)> renameClip;
    // The session's undo, for the window's Edit menu and keys; the labels are
    // kept up to date by the window.
    std::function<void()> undo, redo;
    std::string           undoLabel, redoLabel;

    // Hearing notes while editing (MILESTONES.md, HE; decided with the author
    // 2026-11-22): a note pressed in the grid sounds while the button is held,
    // at its own velocity and its starting pitch, whether or not the host
    // plays; dragged to another row, it sounds at the new pitch. Sent through
    // `hear` (the take's synth in the plugin, the clip's track's output in
    // the app) on its own channel, kHearChannel, so it never ends a note the
    // clip is playing. Unset: silent, and no menu item.
    static constexpr std::uint8_t kHearGroup = 0, kHearChannel = 15;
    std::function<void(const uw::MidiMessage* messages, std::size_t count)> hear;
    // Where Edit > Hear notes while editing is kept (the plugin's state, the
    // app's session); null: the model's own. On by default.
    bool* hearSwitch = nullptr;
    bool  hearing() const noexcept { return static_cast<bool>(hear) && (hearSwitch != nullptr ? *hearSwitch : ownHear); }
    void  setHearing(bool on);
    // The note at `index` sounds now (ending the one before), unless it's
    // sounding at that pitch already.
    void  hearNote(int index);
    // A row sounds, as a note added there would (a press on empty grid).
    void  hearRow(int row, int velocity = 100);
    void  stopHearing();   // the note sounding ends (the button let go)
    // A note's pitch as it starts, in 7.9: the clip's own while it's on the
    // row it came from, else its row's.
    std::uint16_t soundingPitch79(const GridNote& n) const noexcept;

    // Where the clip's tick 0 is on the grid, in ticks (0 in the app). The
    // plugin's grid is in the song's time, from the bar its take starts in,
    // so its columns and bar lines are the host's (milestone P3): there
    // `origin` is the take's start less that bar's. A note's start here is
    // in the grid's ticks; what commit() gives `editClip` is in the clip's,
    // so a note moved before `origin` has a negative start. Set it before
    // showClip().
    Tick origin = 0;

    std::vector<GridNote> notes;
    int                   focus = -1;   // the note the inspector shows (one of the selected), or -1
    int                   steps = 64;   // the grid's columns: four bars of sixteenths

    // Changes since the start: a view that drew `version()` needn't draw again.
    std::uint64_t version() const noexcept { return changes; }
    void          touch() noexcept { ++changes; }

    int    rows() const noexcept { return 128 * edo / 12; }
    int    referenceRow() const noexcept { return referenceNote * edo / 12; }
    double rowSemitones(int row) const noexcept { return referenceNote + (row - referenceRow()) * 12.0 / edo; }
    // A row's pitch in 7.9, as a note moved there is sent.
    std::uint16_t rowPitch79(int row) const noexcept;
    // The row nearest a pitch in 7.9.
    int    rowFor(std::uint16_t pitch79) const noexcept;
    Tick   endTick() const noexcept { return steps * kStep; }
    // A new tuning: the clip shown (if any, unedited) is laid out again in it;
    // edited notes keep their pitch on the nearest row.
    void   setTuning(int edo, int referenceNote);
    // Whether the notes are still the shown clip's, unedited.
    bool   showingClip() const noexcept { return shown != nullptr && !pending; }
    const std::shared_ptr<const Clip>& clip() const noexcept { return shown; }

    GridNote*       selectedNote() noexcept;   // the focus
    const GridNote* selectedNote() const noexcept;
    int             selectionSize() const noexcept;

    // The topmost note covering `tick` in row `pitch`, or -1.
    int noteAt(int pitch, Tick tick) const noexcept;

    // --- selecting (not edits) ---
    void select(int index) noexcept;   // only it (-1: nothing)
    void toggle(int index) noexcept;   // Shift+click: in or out of the selection
    // The notes touching rows [rowLow, rowHigh] and ticks [from, to): only
    // them, or added to the selection.
    void selectIn(int rowLow, int rowHigh, Tick from, Tick to, bool add) noexcept;
    // Every note (Edit > Select all notes, Cmd/Ctrl+A). The focus stays if
    // there is one, else it's the last note, as a box's.
    void selectAll() noexcept;

    // --- edits: each shows at once; commit() makes them the clip's ---
    // Adds a note at `pitch` and `step` (a column), kept inside the grid, and
    // selects only it. Returns its index, or -1 if `step` is past the end.
    int  addNote(int pitch, int step, int lengthSteps = 2, int velocity = 100);
    void deleteSelected();
    void clear();   // every note
    // A drag of the selection, from where it was at beginDrag(), by `ticks`
    // and `rows`. Snapped, the canvas gives whole sixteenths, so each note
    // keeps its offset from the grid: moving a recorded note isn't
    // quantizing it (decisions.md, "The clip editor"). `toGrid` (Ctrl+drag)
    // puts the focus note's start on the nearest sixteenth, and the rest of
    // the selection moves with it. The whole selection stays inside the grid.
    void beginDrag();
    void dragMove(Tick ticks, int rows, bool toGrid = false);
    // The same for the ends (`toGrid`: the focus note's end on the nearest
    // sixteenth). A note is at least a tick long (snapped: a sixteenth, or as
    // long as it was if shorter).
    void dragResize(Tick ticks, bool snap, bool toGrid = false);
    // The selection moved by `ticks` and `rows`, at once (the arrow keys).
    void nudge(Tick ticks, int rows);
    // The focus note's start (in ticks), row and length, and the selection
    // with it by as much (the inspector's sliders).
    void moveSelected(Tick start, int pitch);
    void transposeSelected(int by);
    void setSelectedLength(Tick length);
    void setSelectedVelocity(int velocity);   // every selected note's
    // Every selected note's start onto the nearest sixteenth, its length kept
    // (Edit > Quantize notes, Q): quantizing chosen notes on purpose, which
    // is printing it (decisions.md, "The clip editor"). Commit it as
    // "Quantize notes".
    void quantizeSelected();

    // The edits since the clip was shown, as one command on it (named for
    // what they did), if editClip is set; then the new clip is shown. Without
    // editClip the edits stay. Returns whether a command was made.
    // `name` names the command instead ("Quantize notes").
    bool commit(const std::string& name = {});
    // The changes commit() would make, and its name ("" for none).
    std::string pendingChanges(std::vector<NoteChange>& changes, std::vector<NewNote>& added) const;

    void setClipName(const std::string& name);

    // The clip's notes in its scale, each on the nearest row (its pitch is its
    // 7.9: notesOf()), and its name; the grid as long as the clip, in whole
    // bars (or as long as it was, if longer: it grows, never shrinks). The selection is kept when the
    // music is the same (a scale or a name set), and `select` (note-ons in the
    // clip) chooses it otherwise.
    void showClip(std::shared_ptr<const Clip> clip, const std::vector<std::size_t>* select = nullptr);
    void newClip();   // empty and untitled, the window's own
    // Shows no clip: no notes, and the grid's length forgotten, so the next
    // clip shown sets it (the plugin's window, choosing another take).
    void release();

private:
    // The shown clip's notes, in the current tuning, those with these note-ons
    // selected.
    void layOut(const std::vector<std::size_t>& selectOn, std::size_t focusOn);
    void selectedOns(std::vector<std::size_t>& ons, std::size_t& focusOn) const;
    void edited() {
        pending = true;
        touch();
    }
    void moveBy(const std::vector<GridNote>& from, Tick ticks, int rows);

    std::shared_ptr<const Clip> shown;      // the clip
    std::vector<GridNote>       original;   // its notes as laid out, for commit()
    std::vector<GridNote>       grabbed;    // the notes at beginDrag()
    bool                        pending = false;   // the notes differ from the clip's
    std::uint64_t               changes = 0;
    void          startHearing(std::uint16_t pitch79, int velocity);
    bool          ownHear   = true;
    bool          heardOn   = false;   // a note is sounding through `hear`
    std::uint8_t  heardKey  = 0;
    std::uint16_t heardPitch = 0;
};

} // namespace umpire
