#pragma once

// What the track view draws (as StageHand's RackViewModel, its
// docs/rack-view.md section 4): plain values, rebuilt by TrackController each
// tick, with no pointers into the tracks. Widgets read it and send intents to
// the controller; they never edit it. Comparable, so the window redraws only
// when it changes.

#include <cstdint>
#include <string>
#include <vector>

#include "App/MidiInputs.h"
#include "App/Track.h"

namespace umpire {

struct TrackRowModel {
    TrackId       id = 0;
    std::string   name;
    MidiSourceRef input;
    std::string   inputLabel;            // "All inputs", "KeyLab 88", "KeyLab 88 (not connected)"
    bool          inputConnected = true;
    std::uint16_t groupMask   = 0xFFFF;
    std::uint16_t channelMask = 0xFFFF;
    bool          armed  = false;
    bool          muted  = false;
    bool          soloed = false;
    std::string   outputLabel = "No output";   // milestones 7 and 8
    bool          midiActive  = false;   // MIDI reached the track in the last moment
    bool          selected    = false;

    bool operator==(const TrackRowModel&) const = default;
};

struct MonitorLine {
    double      seconds = 0;   // since the first message the monitor saw
    std::string source;        // the input's name
    std::string words;         // the packet, in hex
    std::string text;          // describeUmp()
    std::string tracks;        // the tracks that took it ("Track 1, Bass"; "" = none)

    bool operator==(const MonitorLine&) const = default;
};

struct TrackViewModel {
    std::vector<TrackRowModel>      rows;
    std::vector<MidiInputs::Choice> inputs;   // for every row's input choice
    std::string                     midiBackend;   // "ALSA sequencer", "CoreMIDI", "no MIDI"
    std::uint64_t                   midiDropped = 0;

    // The monitor: the latest lines, oldest first, and how many in all.
    std::vector<MonitorLine> monitor;
    std::uint64_t            monitorCount = 0;

    // Timeline: bars of 4/4 until milestone 6's transport has a meter.
    int beatsPerBar = 4;

    bool operator==(const TrackViewModel&) const = default;
};

} // namespace umpire
