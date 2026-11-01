#pragma once

// Editing a clip's notes (milestone 10, docs/clip-editor.md). A clip never
// changes once made, so an edit makes a new clip from the old one and a list
// of changes. No toolkit types: the grid (GridModel) says what changed, and
// TrackController makes it a command.
//
// A note is what notesOf() finds: its note-on, its note-off, and the per-note
// messages for its key in between (and those at its start, just before its
// note-on). They all go with it when it moves, re-addressed if its key changes,
// so per-note data survives. A Pitch 7.25 sent for its key earlier is the
// key's, not the note's, and stays.
//
// Pitch: a note keeps its exact pitch unless the change gives it another. A
// new pitch is written the way the note came: a Pitch 7.9 attribute
// rewritten, or a Pitch 7.25 sent with it rewritten; a plain note number stays
// one if the pitch is a whole 12-EDO note and that key is free. Otherwise it's
// an attribute on the free note number nearest the pitch (StageHand's rule).
// Afterwards, every note the edit didn't touch sounds as before: where one
// wouldn't (a 7.25 that tuned its key moved away), it gets an attribute.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "App/Clip.h"

namespace umpire {

// A change to one of a clip's notes, named by its note-on (NoteSpan::on).
struct NoteChange {
    std::size_t   on     = 0;
    bool          remove = false;
    Tick          start  = 0;
    Tick          length = 1;          // at least one tick
    std::uint16_t pitch79  = 60 << 9;  // the pitch it sounds at, as NoteSpan
    std::uint16_t velocity = 0;        // 16-bit
};

// A note to add: a MIDI 2.0 note on the clip's first note's group and channel.
struct NewNote {
    Tick          start  = 0;
    Tick          length = 1;
    std::uint16_t pitch79  = 60 << 9;
    std::uint16_t velocity = 0x8000;
};

struct EditedClip {
    std::shared_ptr<const Clip> clip;   // null: nothing changed
    // Where each kept change's note, then each new note, is now: its note-on
    // in the new clip's events, in the order given.
    std::vector<std::size_t> placed;
};

// The clip with the changes made: its name and scale, a length grown to its
// last note's end, and the id `id`. No clip when nothing changes.
EditedClip editNotes(const Clip& clip, ClipId id, const std::vector<NoteChange>& changes,
                     const std::vector<NewNote>& added = {});

bool sameMessage(const uw::MidiMessage& a, const uw::MidiMessage& b) noexcept;
bool sameEvents(const std::vector<ClipEvent>& a, const std::vector<ClipEvent>& b) noexcept;

} // namespace umpire
