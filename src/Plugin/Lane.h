#pragma once

// The plugin's lane (milestone P2, docs/plugin-version.md, "P2"): every take
// recorded, as clips placed on the host's timeline, and what of them plays.
// No CLAP, no threads: Plugin/LaneKeeper.h runs it on the lane thread.
//
// Takes are kept oldest first. Where takes overlap, the newest plays and the
// older ones are kept underneath, not cut, for P3's editor to show (and bring
// back: bringToFront()).

#include <cstddef>
#include <memory>
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
    // Replaces every take (a loaded state). Ids are given anew; a take keeps
    // its name, and one without is named "Take n". The next take is
    // `nextNumber`, or one past the highest "Take n" if that's more (0: that).
    void setTakes(std::vector<ClipRef> takes, int nextNumber = 0);
    // The number the next take recorded is named with ("Take n"), for the state.
    int nextTakeNumber() const { return nextNumber; }
    // A clip from a file (P4: Import clip), added as the newest at `start`,
    // with its name (or "Take n" if it has none) and its scale worked out.
    // Its id, or 0 if it has no events.
    ClipRefId addClip(Tick start, Clip clip);

    // --- P3: the editor's edits. Each keeps the take's id and place; false
    // if there's no such take. ---
    const ClipRef* find(ClipRefId ref) const;
    // The take's clip and start replaced (an edit in the grid).
    bool replace(ClipRefId ref, Tick start, std::shared_ptr<const Clip> clip);
    bool remove(ClipRefId ref);
    // Made the newest, so it's heard wherever it overlaps others.
    bool bringToFront(ClipRefId ref);
    // The takes as they were before (undo): their ids and names kept.
    void restore(std::vector<ClipRef> takes);
    // An id for an edited clip (a clip never changes once made: an edit
    // makes a new one, docs/clip-editor.md).
    ClipId newClipId() { return nextClip++; }

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

// Where a take ends on the lane: its length, or its last event if later.
Tick takeEnd(const ClipRef& take);

// The parts of takes[index] that are heard, in song ticks, in order: its
// span less the spans of newer takes (what flatten() plays). The rest of its
// span is covered (the editor draws it as a strip underneath).
struct Span {
    Tick start = 0, end = 0;
};
std::vector<Span> visibleSpans(const std::vector<ClipRef>& takes, std::size_t index);

} // namespace umpire::plugin
