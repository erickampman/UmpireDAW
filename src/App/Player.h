#pragma once

// Playback to MIDI outputs and plugin instruments (milestones 7 and 8,
// docs/playback.md, docs/plugins.md): each track with an output plays its
// clips there, scheduled a little ahead on uw-core's clock so their timing
// doesn't hang on the main loop's 10 ms tick. A MIDI device holds them until
// their time (uw::MidiOutput::sendAt); a plugin voice does the same in its
// own queue, turned into a sample-accurate offset when its block comes
// (AudioEngine). And what's played live goes through to the touched track's
// output, whichever kind it is.
//
// Per-key Pitch 7.25 is chased: the synths keep it as a tuning table, so on
// play, a locate and a loop wrap, each key a track's clips tune gets the
// Pitch 7.25 in force at the playhead, or a Per-Note Management reset where
// none is. Notes sounding are ended on stop, a locate and a wrap; nothing
// else is chased yet (controllers, programs).
//
// Each track has its own stream: a handle to its device or its plugin voice
// (one per stream, so cancelling one leaves the rest), what's been scheduled
// on it, and the notes sounding. Main thread only; no toolkit types.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "App/AudioEngine.h"
#include "App/MidiOutputs.h"
#include "App/Session.h"
#include "App/Transport.h"

namespace umpire {

class Player {
public:
    // How far ahead clips are sent, where the platform holds them until their
    // time: covers a slow frame or two of the main loop.
    static constexpr uw::Nanoseconds kLookahead = 100'000'000;

    Player(MidiOutputs& outputs, AudioEngine& engine);
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

    // The track's plugin editor (milestone 11): false if its output isn't a
    // plugin, or that voice isn't open, or the plugin has no editor.
    bool openEditor(TrackId track, const std::string& title, std::function<void()> requestQuit = {});
    void closeEditor(TrackId track);
    bool editorOpen(TrackId track) const;

private:
    struct Sounding {
        int             key = 0;   // group, channel and note
        uw::Nanoseconds on = 0, off = kNever;
    };
    static constexpr uw::Nanoseconds kNever = INT64_MAX;

    struct Stream {
        std::string               device;
        bool                      plugin      = false;   // the device names a plugin (Instruments::isPluginOutput)
        uw::MidiDestinationHandle midiHandle  = uw::kInvalidMidiDestination;
        VoiceHandle                pluginVoice = kInvalidVoice;
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

    // Dispatch to `outputs` or `engine`, by whether the stream's device names
    // a plugin (Instruments::isPluginOutput): the only places that know it.
    void              openStream(Stream& s);
    void              closeStream(Stream& s);
    bool              streamOpen(const Stream& s) const;
    bool              streamSchedules(const Stream& s) const;
    uw::MidiProtocol  streamProtocol(const Stream& s) const;
    void              streamSendAt(Stream& s, const uw::MidiMessage* messages, std::size_t count, uw::Nanoseconds when);
    void              streamCancel(Stream& s);

    MidiOutputs&               outputs;
    AudioEngine&               engine;
    std::map<TrackId, Stream>  streams;
    std::map<int, TrackId>     thruHeld;   // live notes on, by key: the track they went to
    std::uint64_t              seenAnchor     = 0;
    std::uint64_t              seenGeneration = 0;
    bool                       wasPlaying     = false;
};

} // namespace umpire
