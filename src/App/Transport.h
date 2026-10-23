#pragma once

// Umpire's transport (roadmap, milestone 6): play, stop, record, locate and a
// tempo, with the position in ticks. It keeps no clock of its own: every call
// takes the time on uw-core's clock (uw::nowNanoseconds(), the one MIDI
// capture times use), so a message's capture time maps to the tick it was
// played at, and tests drive it with a fake one.
//
// The position runs from an anchor: the tick and time where it last started,
// was located or changed tempo.

#include "App/Clip.h"
#include "uw/Types.h"

namespace umpire {

class Transport {
public:
    bool   playing() const noexcept { return isPlaying; }
    bool   recording() const noexcept { return isRecording; }
    double tempo() const noexcept { return bpm; }

    // The tick at time `at`: where the playhead was, is, or will be.
    Tick tickAt(uw::Nanoseconds at) const noexcept {
        if (!isPlaying) return anchorTick;
        const double beats = static_cast<double>(at - anchorTime) * bpm / 60e9;
        return anchorTick + static_cast<Tick>(beats * static_cast<double>(kTicksPerQuarter));
    }

    void play(uw::Nanoseconds now) noexcept {
        if (isPlaying) return;
        anchorTime = now;
        isPlaying  = true;
    }
    // Stops where it is, and stops recording.
    void stop(uw::Nanoseconds now) noexcept {
        anchorTick  = tickAt(now);
        isPlaying   = false;
        isRecording = false;
    }
    void setRecording(bool on) noexcept { isRecording = on; }
    void locate(Tick to, uw::Nanoseconds now) noexcept {
        anchorTick = to < 0 ? 0 : to;
        anchorTime = now;
    }
    // Keeps the position: the new tempo runs from now.
    void setTempo(double beatsPerMinute, uw::Nanoseconds now) noexcept {
        anchorTick = tickAt(now);
        anchorTime = now;
        bpm        = beatsPerMinute;
    }

private:
    double          bpm         = 120;
    bool            isPlaying   = false;
    bool            isRecording = false;
    Tick            anchorTick  = 0;
    uw::Nanoseconds anchorTime  = 0;
};

} // namespace umpire
