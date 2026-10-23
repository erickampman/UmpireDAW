#pragma once

// The session: the tracks, in the order the track view draws them, their
// clips, and the tempo. Plain values, no toolkit types. Every change to it is
// a command (TrackController::command), so undo covers everything
// (decisions.md, "The UI direction"). What isn't the music stays out: the
// touched track, the selected clip, the playhead.
//
// Where MIDI comes from belongs to the session, not a track (ui-direction.md,
// "Input belongs to the session"): Umpire listens to every input. Where a
// track's MIDI goes comes with milestones 7 and 8, as edges of the graph
// rather than fields here.

#include <cstdint>
#include <string>
#include <vector>

#include "App/Clip.h"

namespace umpire {

using TrackId = std::uint32_t;

struct Track {
    TrackId              id = 0;   // stable for the track's life, never reused in a session
    std::string          name;
    bool                 armed  = false;   // records
    bool                 muted  = false;   // plays nothing (milestone 7)
    bool                 soloed = false;
    std::vector<ClipRef> clips;            // on its lane, in the order they were added

    bool operator==(const Track&) const = default;
};

struct Session {
    std::vector<Track> tracks;
    double             tempoBpm    = 120;
    int                beatsPerBar = 4;   // quarter notes; a meter comes later

    Track* track(TrackId id) {
        for (Track& t : tracks)
            if (t.id == id) return &t;
        return nullptr;
    }
    const Track* track(TrackId id) const { return const_cast<Session*>(this)->track(id); }

    bool operator==(const Session&) const = default;
};

} // namespace umpire
