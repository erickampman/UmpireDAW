#pragma once

// The plugin's lane (milestone P2, docs/plugin-version.md, "P2"): every take
// recorded, as clips placed on the host's timeline, and what of them plays.
// No CLAP, no threads: Plugin/LaneKeeper.h runs it on the lane thread.
//
// Takes are kept oldest first. Where takes overlap, the newest plays and the
// older ones are kept underneath, not cut, for P3's editor to show.

#include <vector>

#include "App/Clip.h"

namespace umpire::plugin {

class Lane {
public:
    // Oldest first. Each ClipRef's start is in the host's ticks (its song
    // position), the clip's events from there.
    const std::vector<ClipRef>& takes() const { return all; }

    // A take recorded from `start`: added as the newest, named "Take n". A
    // take with nothing in it leaves nothing (false).
    bool addTake(Tick start, Clip clip);
    // Replaces every take (a loaded state). Names and ids are given anew.
    void setTakes(std::vector<ClipRef> takes);

private:
    std::vector<ClipRef> all;
    ClipRefId            nextRef  = 1;
    ClipId               nextClip = 1;
    int                  nextNumber = 1;
};

// What plays, in song ticks, in time order: each take's visible pieces (its
// span less the spans of newer takes). A note sounding where a piece is cut
// ends there; a note that began under a newer take isn't started where its
// piece resumes; and at a piece's start after the take's own, each key the
// take tunes gets the Pitch 7.25 in force there, or a Per-Note Management
// reset. At one tick, what ends a piece comes before what starts another.
std::vector<ClipEvent> flatten(const std::vector<ClipRef>& takes);

} // namespace umpire::plugin
