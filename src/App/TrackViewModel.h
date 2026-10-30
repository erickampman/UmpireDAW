#pragma once

// What the track view draws (as StageHand's RackViewModel, its
// docs/rack-view.md section 4): plain values, rebuilt by TrackController each
// tick, with no pointers into the session. Widgets read it and send intents to
// the controller; they never edit it. Comparable, so the window redraws only
// when it changes.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "App/Clip.h"
#include "App/Session.h"

namespace umpire {

// A clip on a lane, or the take being recorded into one.
struct ClipModel {
    ClipRefId   ref = 0;   // 0 for a take
    std::string name;
    Tick        start  = 0;
    Tick        length = 0;
    bool        selected  = false;
    bool        recording = false;   // a take, growing
    // Its notes, from the clip's start; shared, so comparing is cheap.
    std::shared_ptr<const std::vector<NoteSpan>> notes;
    int lowPitch = 60 << 9, highPitch = 60 << 9;   // the range they span, in 7.9

    bool operator==(const ClipModel&) const = default;
};

// A track's row. The row shows only what isn't the default (ui-direction.md):
// a light, the name, and badges; the touched track shows the rest in place.
struct TrackRowModel {
    TrackId     id = 0;
    std::string name;
    bool        armed  = false;
    bool        muted  = false;
    bool        soloed = false;
    std::string outputLabel = "No output";   // its device's name; a plugin with milestone 8
    bool        outputMissing = false;       // its device isn't connected now
    bool        midiActive  = false;   // MIDI reached the track in the last moment
    bool        touched     = false;   // the selected track: playing goes to it
    std::vector<ClipModel> clips;

    bool operator==(const TrackRowModel&) const = default;
};

struct TransportModel {
    bool   playing   = false;
    bool   recording = false;
    Tick   position  = 0;   // updated about 30 times a second while playing
    double tempoBpm  = 120;
    bool   looping   = false;
    Tick   loopStart = 0, loopEnd = 0;   // loopEnd == loopStart: no loop set
    std::string positionLabel = "1.1.1";   // bar.beat.sixteenth
    std::string timeLabel     = "0:00.0";

    bool operator==(const TransportModel&) const = default;
};

struct MonitorLine {
    double      seconds = 0;   // since the first message the monitor saw
    std::string source;        // the input's name
    std::string words;         // the packet, in hex
    std::string text;          // describeUmp()
    std::string tracks;        // the tracks that took it ("Track 1, Bass"; "" = none)

    bool operator==(const MonitorLine&) const = default;
};

// A device a track can play to.
struct OutputChoice {
    std::string label;
    std::string device;   // its id, for TrackController::setOutput
    bool        hardware = false;

    bool operator==(const OutputChoice&) const = default;
};

struct TrackViewModel {
    std::vector<TrackRowModel> rows;
    std::vector<OutputChoice>  outputs;   // where a track can play to, hardware first
    TransportModel             transport;
    int                        beatsPerBar = 4;

    // The status line: what's listening and where playing goes.
    std::string listening;   // "KeyLab 88, MIDI 2.0 In" or "no inputs"
    std::string status;      // "Playing goes to Keys" or "Recording to Keys, Bass"
    bool        warning = false;   // status is a problem ("Arm a track to record")

    std::string undoLabel, redoLabel;   // "" when there's nothing to undo or redo

    std::string   midiBackend;   // "ALSA sequencer", "CoreMIDI", "no MIDI"
    std::uint64_t midiDropped = 0;

    // The monitor: the latest lines, oldest first, and how many in all.
    std::vector<MonitorLine> monitor;
    std::uint64_t            monitorCount = 0;

    bool operator==(const TrackViewModel&) const = default;
};

} // namespace umpire
