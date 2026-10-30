#pragma once

// Playback to MIDI outputs (milestone 7, docs/playback.md): each track with
// an output plays its clips there, scheduled a little ahead on uw-core's
// clock so their timing doesn't hang on the main loop's 10 ms tick; the
// platform holds them until their time (uw::MidiOutput::sendAt). And what's
// played live goes through to the touched track's output.
//
// Per-key Pitch 7.25 is chased: the synths keep it as a tuning table, so on
// play, a locate and a loop wrap, each key a track's clips tune gets the
// Pitch 7.25 in force at the playhead, or a Per-Note Management reset where
// none is. Notes sounding are ended on stop, a locate and a wrap; nothing
// else is chased yet (controllers, programs).
//
// Each track has its own stream: a handle to its device (MidiOutputs opens
// one per stream, so cancelling one leaves the rest), what's been scheduled
// on it, and the notes sounding. Main thread only; no toolkit types.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "App/MidiOutputs.h"
#include "App/Session.h"
#include "App/Transport.h"

namespace umpire {

class Player {
public:
    // How far ahead clips are sent, where the platform holds them until their
    // time: covers a slow frame or two of the main loop.
    static constexpr uw::Nanoseconds kLookahead = 100'000'000;

    explicit Player(MidiOutputs& outputs);
    ~Player();

    // Each tick: follow the session (outputs, mute and solo, clips) and the
    // transport, then send what plays up to kLookahead from now.
    void tick(const Session& session, const Transport& transport, uw::Nanoseconds now);

    // The transport started, or jumped (a locate) while playing: notes
    // sounding end, Pitch 7.25 is chased at the playhead, and playing goes on
    // from it.
    void started(const Session& session, const Transport& transport, uw::Nanoseconds now);
    // The transport stopped: what's scheduled is dropped and notes sounding end.
    void stopped(const Transport& transport, uw::Nanoseconds now);

    // Played live (MIDI 2.0), through to `track`'s output unless it's muted.
    // A note-off goes where its note-on went, even if the touched track has
    // changed since.
    void thru(TrackId track, const uw::MidiMessage* messages, std::size_t count);

    // A track's output, for its row: the device's name and whether it's there.
    std::string outputLabel(const Session& session, TrackId track, bool* connected) const;

private:
    struct Sounding {
        int             key = 0;   // group, channel and note
        uw::Nanoseconds on = 0, off = kNever;
    };
    static constexpr uw::Nanoseconds kNever = INT64_MAX;

    struct Stream {
        std::string               device;
        uw::MidiDestinationHandle handle  = uw::kInvalidMidiDestination;
        bool                      audible = false;   // not muted, and soloed if anything is
        bool                      muted   = false;
        std::vector<ClipRef>      clips;             // what it plays, to see a change
        Tick                      frontier = 0;      // raw ticks sent up to
        std::vector<Sounding>     sounding;          // notes sent and not ended
    };

    void follow(const Session& session, const Transport& transport, uw::Nanoseconds now, bool rejoin = true);
    void join(Stream& s, const Track& track, const Transport& transport, uw::Nanoseconds now);
    void silence(Stream& s, uw::Nanoseconds now);
    void reschedule(Stream& s, const Transport& transport, uw::Nanoseconds now);
    void schedule(Stream& s, const Track& track, const Transport& transport, uw::Nanoseconds now);
    void chase(Stream& s, const Track& track, Tick at, uw::Nanoseconds when);
    void endSounding(Stream& s, uw::Nanoseconds when);
    void thruOff(TrackId track);
    void send(Stream& s, const uw::MidiMessage* messages, std::size_t count, uw::Nanoseconds when);

    MidiOutputs&               outputs;
    std::map<TrackId, Stream>  streams;
    std::map<int, TrackId>     thruHeld;   // live notes on, by key: the track they went to
    std::uint64_t              seenAnchor     = 0;
    std::uint64_t              seenGeneration = 0;
    bool                       wasPlaying     = false;
};

} // namespace umpire
