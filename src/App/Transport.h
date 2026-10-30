#pragma once

// Umpire's transport (roadmap, milestones 6 and 7): play, stop, record,
// locate, loop and a tempo, with the position in ticks. It keeps no clock of
// its own: every call takes the time on uw-core's clock (uw::nowNanoseconds(),
// the one MIDI capture times use), so a message's capture time maps to the
// tick it was played at, and tests drive it with a fake one.
//
// The position runs from an anchor: the tick and time where it last started,
// was located, or changed tempo, loop or recording. From the anchor it counts
// raw ticks, which never jump; the position is a raw tick wrapped by the loop.
// Playback schedules in raw ticks (runs()), so a wrap ahead of the playhead is
// planned like anything else.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "App/Clip.h"
#include "uw/Types.h"

namespace umpire {

class Transport {
public:
    // A stretch the playhead plays without a jump: ticks [begin, end) of the
    // timeline, starting `raw` ticks into the anchor's count. `wrapped`: it
    // starts where the loop sent the playhead back.
    struct Run {
        Tick begin = 0, end = 0;
        Tick raw   = 0;
        bool wrapped = false;
        bool operator==(const Run&) const = default;
    };

    bool   playing() const noexcept { return isPlaying; }
    bool   recording() const noexcept { return isRecording; }
    double tempo() const noexcept { return bpm; }
    bool   looping() const noexcept { return loopOn; }
    Tick   loopStart() const noexcept { return loopFrom; }
    Tick   loopEnd() const noexcept { return loopTo; }
    // Goes up with every new anchor: what was scheduled from the old one is
    // out of date.
    std::uint64_t anchor() const noexcept { return anchors; }

    // The raw tick at time `at`: the anchor's tick plus the ticks since.
    Tick rawAt(uw::Nanoseconds at) const noexcept {
        if (!isPlaying) return anchorTick;
        const double beats = static_cast<double>(at - anchorTime) * bpm / 60e9;
        return anchorTick + static_cast<Tick>(beats * static_cast<double>(kTicksPerQuarter));
    }
    // The tick at time `at`: where the playhead was, is, or will be.
    Tick tickAt(uw::Nanoseconds at) const noexcept { return wrap(rawAt(at)); }
    // When raw tick `raw` plays (playing).
    uw::Nanoseconds timeOfRaw(Tick raw) const noexcept {
        return anchorTime + static_cast<uw::Nanoseconds>(static_cast<double>(raw - anchorTick) * 60e9 /
                                                         (bpm * static_cast<double>(kTicksPerQuarter)));
    }
    // When tick `tick` of `run` plays.
    uw::Nanoseconds timeOf(const Run& run, Tick tick) const noexcept { return timeOfRaw(run.raw + tick - run.begin); }

    // What plays from raw tick `from` to `to`, in order: one run, or more
    // where the loop wraps.
    std::vector<Run> runs(Tick from, Tick to) const {
        std::vector<Run> out;
        for (Tick r = from; r < to;) {
            const Tick p    = wrap(r);
            const Tick next = wraps() ? std::min(to, r + (loopTo - p)) : to;
            out.push_back(Run{p, p + (next - r), r, wraps() && r >= loopTo && p == loopFrom});
            r = next;
        }
        return out;
    }

    void play(uw::Nanoseconds now) noexcept {
        if (isPlaying) return;
        anchorTime = now;
        isPlaying  = true;
        ++anchors;
    }
    // Stops where it is, and stops recording.
    void stop(uw::Nanoseconds now) noexcept {
        anchorTick  = tickAt(now);
        isPlaying   = false;
        isRecording = false;
        ++anchors;
    }
    void setRecording(bool on, uw::Nanoseconds now) noexcept {
        reanchor(now);
        isRecording = on;
    }
    void locate(Tick to, uw::Nanoseconds now) noexcept {
        anchorTick = to < 0 ? 0 : to;
        anchorTime = now;
        ++anchors;
    }
    // Keeps the position: the new tempo runs from now.
    void setTempo(double beatsPerMinute, uw::Nanoseconds now) noexcept {
        reanchor(now);
        bpm = beatsPerMinute;
    }
    // Keeps the position. A playhead past the loop's end plays on; one before
    // it goes back when it gets there.
    void setLoop(bool on, Tick start, Tick end, uw::Nanoseconds now) noexcept {
        reanchor(now);
        loopOn   = on;
        loopFrom = std::max<Tick>(0, start);
        loopTo   = std::max(loopFrom, end);
    }

private:
    void reanchor(uw::Nanoseconds now) noexcept {
        anchorTick = tickAt(now);
        anchorTime = now;
        ++anchors;
    }
    // Whether raw ticks from this anchor wrap at the loop's end.
    bool wraps() const noexcept {
        return isPlaying && loopOn && !isRecording && loopTo > loopFrom && anchorTick < loopTo;
    }
    Tick wrap(Tick raw) const noexcept {
        if (!wraps() || raw < loopTo) return raw;
        return loopFrom + (raw - loopTo) % (loopTo - loopFrom);
    }

    double          bpm         = 120;
    bool            isPlaying   = false;
    bool            isRecording = false;
    bool            loopOn      = false;
    Tick            loopFrom    = 0;
    Tick            loopTo      = 0;
    Tick            anchorTick  = 0;
    uw::Nanoseconds anchorTime  = 0;
    std::uint64_t   anchors     = 0;
};

} // namespace umpire
