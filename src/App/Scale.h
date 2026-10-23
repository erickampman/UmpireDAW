#pragma once

// A clip's scale, worked out from its notes (decisions.md, "Time, clips and
// pitch"; ideas.md, "Working out a clip's scale"). Every scale is an EDO for
// now: `edo` equal steps to the octave, one of them on `referenceNote`, as
// StageHand's sequences and KeyboardEDO's tunings. No toolkit types.

#include <cstdint>
#include <string>
#include <vector>

namespace umpire {

struct Scale {
    static constexpr int kMinEdo = 5, kMaxEdo = 72;

    int edo           = 12;
    int referenceNote = 60;   // a MIDI note on the lattice: KeyboardEDO's anchor, or one equivalent

    double semitones(int step) const { return referenceNote + step * 12.0 / edo; }
    bool   operator==(const Scale&) const = default;
};

// How a clip's scale was arrived at, for the grid to say.
enum class ScaleFit {
    Default,     // no notes to work it out from: 12-EDO from 60
    WorkedOut,   // one EDO fits the notes
    Unsure,      // several unrelated EDOs fit (too few notes): the smallest, for the user to confirm
    NoFit,       // no EDO up to 72 fits: 12-EDO from 60, notes on the nearest rows
    Chosen,      // the user set it
};

struct ScaleGuess {
    Scale            scale;
    ScaleFit         fit = ScaleFit::Default;
    std::vector<int> candidates;   // the EDOs that fit, none a multiple of another

    bool operator==(const ScaleGuess&) const = default;
};

// A pitch as KeyboardEDO labels one: the nearest 12-EDO note and how far from
// it, within 50 cents: "A1 +42 cents" ("C4" when on it). Semitones above
// MIDI note 0.
std::string pitchName(double semitones);

// The scale a set of note pitches (in 7.9, as NoteSpan::pitch79) sit on. An
// EDO fits when every pitch is a whole number of its steps from the others
// and its lattice meets a whole MIDI note (KeyboardEDO anchors at a 12-EDO
// pitch), within a pitch's rounding to 7.9. 12-EDO wins whenever it fits (a
// whole-tone tune also fits 6); otherwise the smallest EDO that fits, sure
// only when no unrelated EDO fits too. The reference is the MIDI note on the
// lattice nearest 60.
ScaleGuess workOutScale(const std::vector<std::uint16_t>& pitches79);

} // namespace umpire
