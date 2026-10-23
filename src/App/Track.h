#pragma once

// A track (decisions.md, "The track view"): where its MIDI comes from, whether
// it records, and what gets its MIDI. Plain values, no toolkit types.

#include <cstdint>
#include <string>

namespace umpire {

// Where MIDI comes from, as a track saves it. Resolved to a live input by
// MidiInputs. Copied from StageHand's Core/MidiSourceRef.h without its
// sequence kind.
struct MidiSourceRef {
    enum class Kind {
        All,       // every open input
        Device,    // one device, by its MidiSourceInfo id
        Virtual,   // one of Umpire's virtual inputs, by name
        None,      // no live input
    };
    Kind        kind = Kind::All;
    std::string name;   // Device: "KeyLab 88:KeyLab 88 MIDI 1"; Virtual: "MIDI 2.0 In"

    bool operator==(const MidiSourceRef&) const = default;
};

using TrackId = std::uint32_t;

struct Track {
    TrackId       id = 0;   // stable for the track's life, never reused in a session
    std::string   name;
    MidiSourceRef input;
    // Which UMP groups and channels it takes, as StageHand's MidiFilter: bit g
    // set = group g, bit c = channel c. Milestone 12 makes a track a view over
    // a group range.
    std::uint16_t groupMask   = 0xFFFF;
    std::uint16_t channelMask = 0xFFFF;
    bool          armed  = false;   // records (milestone 6)
    bool          muted  = false;   // plays nothing (milestone 7)
    bool          soloed = false;
    // What gets its MIDI: a MIDI device (milestone 7) or a CLAP plugin
    // (milestone 8). None until then.

    bool operator==(const Track&) const = default;
};

} // namespace umpire
