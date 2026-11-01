#pragma once

// A clip (roadmap, milestone 6): UMP messages with timestamps in ticks from
// the clip's start. No toolkit types. A take is stored as MIDI 2.0 (MIDI 1.0
// input is upconverted as it's recorded); everything a channel sends is kept,
// per-note controllers and Per-Note Management included, so per-note data
// (decisions.md, "Deferred") needs no change here when it's drawn and edited.
//
// A clip never changes once made: the session shares it between its
// references and between undo's snapshots. Editing (milestone 10) makes a new
// one.
//
// A clip sits on a lane through a ClipRef: where it starts, and later the
// reference's transforms (ideas.md, 9) and its own instrument
// (ui-direction.md, "A clip can choose its own instrument").

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "App/Scale.h"
#include "uw/ump/MidiMessage.h"

namespace umpire {

// Musical time. Ticks are canonical (ideas.md, "Timebase and format facts");
// seconds are derived from the tempo. 40320 (8!) divides by 1 to 10, 12, 14,
// 15 and 16, so triplets, quintuplets and septuplets land exactly, and it fits
// a MIDI Clip File's 16-bit ticks per quarter note (DCTPQ).
using Tick = std::int64_t;
inline constexpr Tick kTicksPerQuarter = 40320;

struct ClipEvent {
    Tick            tick = 0;   // from the clip's start
    uw::MidiMessage message;
};

using ClipId = std::uint32_t;

struct Clip {
    ClipId                 id = 0;
    std::string            name;
    Tick                   length = 0;
    std::vector<ClipEvent> events;   // in time order
    // Its scale (decisions.md, "Time, clips and pitch"): worked out from its
    // notes when it's made (workOutScale), or set by the user.
    Scale                  scale;
    ScaleFit               scaleFit = ScaleFit::Default;
    std::vector<int>       scaleCandidates;   // the EDOs that fit (workOutScale)
};

// The scale `clip`'s notes sit on, into its scale fields.
void workOutScale(Clip& clip);

// A note as the lanes and the grid draw it: from its note-on to its note-off,
// at the pitch it sounds. No tuning is assumed (decisions.md, "Pitch"): the
// pitch is absolute, in 7.9 (512 to a 12-TET semitone, as Pitch 7.9).
// Where a note's pitch came from.
enum class PitchFrom {
    NoteNumber,   // nothing else: the note number, in 12-EDO
    Attribute,    // the note-on's Pitch 7.9 attribute
    Pitch725,     // a Pitch 7.25 (registered per-note controller 3) sent for its key before
};

struct NoteSpan {
    Tick          start  = 0;
    Tick          length = 0;
    int           note   = 60;    // the note number, which names the note, not its pitch
    std::uint16_t pitch79  = 60 << 9;
    std::uint16_t velocity = 0;   // 16-bit
    PitchFrom     from     = PitchFrom::NoteNumber;
    // Its note-on and note-off in the events (npos: none, a note held to the
    // end), so an edit (ClipEdit.h) can find what it was sent as.
    std::size_t   on  = npos;
    std::size_t   off = npos;

    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    bool operator==(const NoteSpan&) const = default;
};

// The notes in `events` (MIDI 1.0 or 2.0), paired by group, channel and note
// number, in order of their starts. A note still held at `end` ends there; a
// note-on for a note already held ends the first. A note's pitch is its Pitch
// 7.9 attribute; without one, the Pitch 7.25 (registered per-note controller
// 3) last sent for its key, until a Per-Note Management reset; without that,
// its note number.
std::vector<NoteSpan> notesOf(const std::vector<ClipEvent>& events, Tick end);

using ClipRefId = std::uint32_t;

struct ClipRef {
    ClipRefId                   id = 0;   // stable, never reused in a session
    std::shared_ptr<const Clip> clip;
    Tick                        start = 0;   // on the timeline

    Tick end() const { return start + (clip ? clip->length : 0); }
    bool operator==(const ClipRef&) const = default;
};

} // namespace umpire
