#pragma once

// The session: the tracks, in the order the track view draws them, their
// clips, and the tempo. Plain values, no toolkit types. Every change to it is
// a command (TrackController::command), so undo covers everything
// (decisions.md, "The UI direction"). What isn't the music stays out: the
// touched track, the selected clip, the playhead.
//
// Where MIDI comes from belongs to the session, not a track (ui-direction.md,
// "Input belongs to the session"): Umpire listens to every input. Where a
// track's MIDI goes is an edge of the graph (Output), not a field of the
// track: a MIDI device (milestone 7) or a CLAP instrument (milestone 8,
// App/Instruments.h), named by the same opaque id either way.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "App/Clip.h"

namespace umpire {

using TrackId = std::uint32_t;

struct Track {
    TrackId              id = 0;   // stable for the track's life, never reused in a session
    std::string          name;
    bool                 armed  = false;   // records
    bool                 muted  = false;   // plays nothing
    bool                 soloed = false;
    std::vector<ClipRef> clips;            // on its lane, in the order they were added

    bool operator==(const Track&) const = default;
};

// An edge from a track to where its MIDI goes: a MIDI device, by its id
// (uw::MidiDestinationInfo::id, "client name:port name" on ALSA), or a CLAP
// instrument, by Instruments::outputId(); kept when it's absent (the device
// unplugged, the plugin uninstalled) so it plays again when it's back.
struct Output {
    TrackId     track = 0;
    std::string device;

    bool operator==(const Output&) const = default;
};

// A plugin output's state (its CLAP state extension, opaque), as a saved
// session keeps it (milestone 9, docs/files.md): for the track's plugin, while
// its output is still `device`. Beside the session, not in it: the plugin
// owns it, and undo doesn't cover it.
struct PluginState {
    std::string            device;
    std::vector<std::byte> data;

    bool operator==(const PluginState&) const = default;
};

struct Session {
    std::vector<Track>  tracks;
    std::vector<Output> outputs;   // at most one per track, for now
    double              tempoBpm    = 120;
    int                 beatsPerBar = 4;   // quarter notes; a meter comes later
    // The loop: while looping, the playhead goes back to loopStart when it
    // reaches loopEnd (not while recording: loop recording comes later).
    bool                looping   = false;
    Tick                loopStart = 0;
    Tick                loopEnd   = 0;

    Track* track(TrackId id) {
        for (Track& t : tracks)
            if (t.id == id) return &t;
        return nullptr;
    }
    const Track* track(TrackId id) const { return const_cast<Session*>(this)->track(id); }
    // The device a track plays to, or null.
    const std::string* outputOf(TrackId id) const {
        for (const Output& o : outputs)
            if (o.track == id) return &o.device;
        return nullptr;
    }

    bool operator==(const Session&) const = default;
};

} // namespace umpire
