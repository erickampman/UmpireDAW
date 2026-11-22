#pragma once

// Umpire as a plugin (docs/plugin-version.md; P1 the spike, P2 recording and
// playback): the audio-thread side, with no CLAP in it, so the tests drive
// it as the host would. The plugin (Plugin/UmpirePlugin.cpp) is the CLAP glue
// around it, and the lane thread (Plugin/LaneKeeper.h) feeds it.
//
// What it does each block, in the host's process() call:
//   - takes live input from the MIDI thread (the plugin's own uw-core input,
//     never the host's MIDI) through a SpscQueue, places each message at a
//     sample offset from its capture time, and plays it through to the synth;
//   - records it into a take while the host records (CLAP_TRANSPORT_IS_RECORDING)
//     and the plugin's Record parameter is on (its arm), in ticks from the
//     host's song position (song_pos_beats); a jump while recording (a locate,
//     a loop wrap) ends the take and starts another;
//   - plays the lane (Plugin/Lane.h's flatten(), in song ticks) from the
//     host's position, ending notes and chasing Pitch 7.25 when the position
//     jumps, as App/Player does on a seek;
//   - follows the host's transport events inside the block (a tempo change),
//     and the arm's changes: each piece of the block is played and recorded at
//     its own position;
//   - runs the hosted synth (a uw::HostedPlugin, uw-core's CLAP host) once
//     for the block, its events at their sample offsets, in MIDI 2.0 where
//     its note port takes it, MIDI 1.0 otherwise (as App/AudioEngine);
//   - NE: first through the note effects in front of it, in order
//     (uw::NoteEffectChain), each in the protocol its note port takes. What's
//     recorded is what's played, before them; the grid's notes (HE) go
//     through them.
//
// Threads: "the lane side" below is any one non-audio thread at a time (the
// lane thread, or the main thread holding the lane's lock); the audio thread
// never allocates or locks. Finished takes cross to the lane side through
// two take buffers, so the next take can start before the last is collected;
// what to play crosses to the audio thread, and back to be freed, through
// SpscQueues.

#include <algorithm>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "App/Clip.h"
#include "Plugin/PluginSetup.h"
#include "uw/base/SpscQueue.h"
#include "uw/midi/MidiInput.h"
#include "uw/plugin/HostedPlugin.h"
#include "uw/plugin/NoteEffectChain.h"
#include "uw/ump/Ump.h"

namespace umpire::plugin {

// The host's transport (CLAP's clap_event_transport), at a block's first
// frame or where a transport event changes it.
struct HostTransport {
    bool   playing   = false;
    bool   recording = false;
    bool   hasBeats  = false;   // songBeats is valid (CLAP_TRANSPORT_HAS_BEATS_TIMELINE)
    double songBeats = 0.0;     // quarter notes from the song's start
    double tempo     = 120.0;   // quarter notes a minute
    int    tsigNum   = 4;
    int    tsigDen   = 4;
    double barStartBeats = 0.0;
    int    barNumber     = 0;
};

// A change inside a block, a transport event or the arm's: from `frame` on,
// the arm is `armed`, and the transport is `transport` if `hasTransport`
// (a transport event); otherwise it goes on as before (an arm change).
struct TransportChange {
    std::uint32_t frame = 0;
    bool          hasTransport = true;
    HostTransport transport;
    bool          armed = false;
};

// One block, as the host gives it.
struct Block {
    std::uint32_t          frames = 0;
    HostTransport          transport;                 // at frame 0
    bool                   armed = false;             // the Record parameter at frame 0: takes record
    const TransportChange* changes     = nullptr;     // in frame order, each after the last
    std::uint32_t          changeCount = 0;
    uw::Nanoseconds        now = 0;                   // when process() was called, on uw-core's clock
    float* const*          out         = nullptr;     // the host's output channels
    std::uint32_t          outChannels = 0;
};

// What the live input's timing looked like, since the last call to takeStats()
// (docs/plugin-version.md, "Timing").
struct LiveStats {
    std::uint64_t count   = 0;
    double        meanMs  = 0;   // capture to the block that plays it
    double        minMs   = 0;
    double        maxMs   = 0;
    std::uint64_t clamped = 0;   // older than a block: played at offset 0
    std::uint64_t dropped = 0;   // the queue or the block's events were full
};

// A take as it ended: its events in ticks from `start` (where recording
// began, on the host's timeline), and its length to where it ended.
// Where a take recorded on play (RecordMode::Play: Logic, GarageBand) starts
// recording, given where the host began playing and where it last stood
// stopped. Those hosts never say they record or count in, but a count-in
// shows: Record with a count-in starts playing a whole number of beats (up
// to six bars) before where the host stood, while Play starts from there. So
// then recording starts where it stood, as the host's own does. Never before
// the song's start (bar 1). Decided with the author, 2026-11-20: nothing is
// recorded before the host itself records (plugin-version.md, "AUv2 for
// Logic").
Tick recordStartOnPlay(Tick playFrom, bool stoppedSeen, Tick stoppedAt, double quartersPerBar);

// How the last take recorded on play began, for the log.
struct TakeStart {
    std::uint64_t serial = 0;   // one more each take recorded on play
    Tick          playFrom = 0, stoppedAt = 0, recordFrom = 0;
    bool          stoppedSeen = false;
};

struct FinishedTake {
    Tick start = 0;
    Clip clip;   // events from `start`; length to the take's end
};

class PluginEngine final : public uw::MidiSink {
public:
    static constexpr std::size_t kMaxEvents     = 2048;      // to the synth in one block (uw::ClapPlugin's limit)
    static constexpr std::size_t kMaxTakeEvents = 1u << 16;  // in one take
    static constexpr int         kKeys          = 16 * 16 * 128;   // group, channel, note
    static constexpr std::size_t kMaxPieces     = 16;        // a block split by its changes; more are ignored

    PluginEngine();
    ~PluginEngine() override;

    PluginEngine(const PluginEngine&)            = delete;
    PluginEngine& operator=(const PluginEngine&) = delete;

    // --- main thread ---

    // The synth the engine plays (null: none, silence). Not owned. Set while
    // inactive.
    void setSynth(uw::HostedPlugin* plugin) { synth = plugin; }
    // NE: the note effects in front of the synth, in order. Not owned; none
    // null. Set while inactive (or swapped in with swapEffects()).
    struct Effect {
        uw::HostedPlugin* plugin   = nullptr;
        bool              bypassed = false;   // runs without MIDI; its MIDI goes around it
        bool operator==(const Effect&) const = default;
    };
    void setEffects(std::vector<Effect> list) { effects = std::move(list); }
    const std::vector<Effect>& currentEffects() const noexcept { return effects; }
    // Another synth, while the engine may be running (P4: the synth chosen in
    // the window). Inactive, it's setSynth() and Done: the old one may go at
    // once. Active, `next` is activated here (its state already loaded) and
    // handed to the audio thread, which takes it at its next block, stops
    // the old one there and hands it back: Pending, and the old one must live
    // until retiredPlugin() returns it. The audio thread never waits for it.
    enum class Swap {
        Done,      // inactive: the old synth may go now
        Pending,   // the audio thread takes it at its next block
        Busy,      // too many swaps not yet taken (the host isn't processing): nothing changed
    };
    Swap swapSynth(uw::HostedPlugin* next);
    // NE: other effects in front of the synth, as swapSynth(): a plugin in
    // both the old and the new chain (the synth, a kept effect) stays active
    // and goes on processing. When the synth stays, the audio thread sends it
    // All Notes Off on every channel as it takes the new chain, since a note's
    // off may now come out of the effects at another key.
    Swap swapEffects(std::vector<Effect> next);
    // A synth or effect the audio thread has let go of since a swap,
    // deactivated now, in neither the synth nor the effects given since, and
    // the caller's to destroy; null when there's none.
    uw::HostedPlugin* retiredPlugin();
    uw::HostedPlugin* currentSynth() const noexcept { return synth; }
    // Allocates for blocks of up to `maxFrames` and activates the synth.
    // False only if the synth wouldn't activate (the engine still runs, silent).
    bool activate(double sampleRate, std::uint32_t maxFrames);
    void deactivate();
    bool active() const noexcept { return isActive; }
    // What the synth's note port takes, once active: MIDI 2.0, or only MIDI 1.0
    // (then it gets the lane downconverted, without per-note pitch).
    bool synthTakesMidi2() const noexcept { return latest != nullptr && latest->midi2; }
    bool synthHasNotes() const noexcept { return latest != nullptr && latest->notePort >= 0; }

    // The render extension: offline, live input is ignored.
    void setOffline(bool offline) { offlineMode.store(offline, std::memory_order_relaxed); }
    bool offline() const { return offlineMode.load(std::memory_order_relaxed); }

    // What starts a take (PluginSetup.h, RecordMode). Host: the host's record
    // button, with the arm; the lane is silent while recording. Play, for
    // hosts that never say they record (Logic): the host playing with the arm
    // on, and the lane plays on meanwhile (an overdub); a pass with no notes
    // leaves no take (Lane::addTake). Off, for live hosts: nothing records,
    // and the lane plays with the host.
    void       setRecordMode(RecordMode mode) { recordModeNow.store(mode, std::memory_order_relaxed); }
    RecordMode recordMode() const { return recordModeNow.load(std::memory_order_relaxed); }

    // --- the lane side ---

    // What the audio thread plays from now on: events in song ticks (a copy
    // is taken). If the audio thread isn't running to take it, it's kept
    // and sent by a later call to collectGarbage().
    void setEvents(const std::vector<ClipEvent>& songEvents);
    // The oldest take that has ended since the last call: false if none.
    // One recorded on play keeps nothing before where recording started
    // (recordStartOnPlay), and starts on its first note's beat.
    bool collectTake(FinishedTake& out);
    TakeStart lastTakeStart() const;
    // Frees what the audio thread has let go of, and sends what setEvents()
    // couldn't. Call now and then.
    void collectGarbage();

    LiveStats takeStats();
    // The host's tempo and meter, last seen (for writing a clip file).
    double lastTempo() const { return seenTempo.load(std::memory_order_relaxed); }
    int    lastBeatsPerBar() const { return seenBeatsPerBar.load(std::memory_order_relaxed); }
    bool   recording() const { return recordingNow.load(std::memory_order_relaxed); }
    // Where the host was at the end of the last block, in song ticks, and
    // whether it was playing then (the editor's playhead, P3).
    Tick   position() const { return seenPosition.load(std::memory_order_relaxed); }
    bool   hostPlaying() const { return seenPlaying.load(std::memory_order_relaxed); }
    // Takes not recorded because both buffers were waiting (the lane side
    // didn't collect them), since the last call.
    std::uint64_t takeMissedCount() { return missedTakes.exchange(0); }

    // --- the main thread: the grid's notes (HE) ---
    // A note the grid sounds while it's pressed (GridModel::hear): played to
    // the synth at the next block's start, never recorded, whether or not
    // the host plays. False if the queue was full (the host isn't processing).
    bool hear(const uw::MidiMessage& message);

    // --- the MIDI thread (uw::MidiSink): one source ---
    void onMidi(uw::MidiSourceHandle source, const uw::TimedMidiMessage& msg) noexcept override;

    // --- the audio thread ---
    void process(const Block& block) noexcept;
    void stopProcessing() noexcept;
    void reset() noexcept;

private:
    struct PlayData {
        std::vector<ClipEvent> events;    // in time order
        std::vector<ClipEvent> tunings;   // Pitch 7.25 and Per-Note Management resets, in time order
        std::vector<int>       tunedKeys;
    };
    struct Out {
        std::uint32_t   offset = 0;
        uw::MidiMessage message;
    };
    // A synth and its effects as the audio thread runs them: made and freed
    // on the main thread, with what they need to run a block.
    struct SynthSlot {
        uw::HostedPlugin*               plugin   = nullptr;
        bool                            active   = false;
        bool                            midi2    = true;
        int                             notePort = -1;
        std::uint32_t                   channels = 0;
        std::vector<std::vector<float>> scratch;
        std::vector<float*>             pointers;
        // NE: the effects that activated, in order; and every plugin this
        // slot holds active (each released when it's retired).
        uw::NoteEffectChain             chain;
        std::vector<uw::HostedPlugin*>  held;
        std::vector<uw::HostedPlugin*>  unheld;   // given, but it wouldn't activate
        bool holds(const uw::HostedPlugin* p) const noexcept {
            return std::find(held.begin(), held.end(), p) != held.end();
        }
    };
    // Activates the synth and the effects (those not active already) and lays
    // out their slot: main thread.
    std::unique_ptr<SynthSlot> makeSlot(uw::HostedPlugin* plugin, const std::vector<Effect>& chain);
    bool acquire(uw::HostedPlugin* plugin);   // activates it the first time: false if it wouldn't
    void release(uw::HostedPlugin* plugin, bool retire = true);   // deactivates it the last time
    bool given(const uw::HostedPlugin* plugin) const noexcept;    // the synth or an effect now
    Swap swapIn(uw::HostedPlugin* nextSynth, std::vector<Effect> nextEffects);
    void collectSlots();                      // frees the slots the audio thread let go of
    void                       adoptSynth() noexcept;
    void                       retireSlot(SynthSlot* slot, bool retire = true);
    enum : int { kTakeIdle = 0, kTakeRecording = 1, kTakeReady = 2 };
    struct TakeBuffer {
        std::atomic<int>       state{kTakeIdle};
        std::uint64_t          serial = 0;   // which ended first
        std::vector<ClipEvent> events;       // song ticks
        std::size_t            count = 0;
        Tick                   start = 0, end = 0;
        bool                   fromFirstNote = false;   // recorded on play: starts on its first note's beat
        Tick                   recordFrom = 0;          // nothing before it is kept (on play: a count-in)
    };
    // One piece of a block: frames [first, first + frames) at one transport.
    struct Piece {
        std::uint32_t first = 0, frames = 0;
        HostTransport transport;
        Tick          tick = 0;            // the song position at `first`
        double        ticksPerFrame = 0;
        bool          armed = false;       // the Record parameter
        bool          continues = false;   // no transport event of its own: on from the last piece
        bool          recording = false;   // live input in it goes to the take
    };

    void   emit(std::uint32_t offset, const uw::MidiMessage& m) noexcept;
    void   endPlayNotes(std::uint32_t offset) noexcept;
    void   chase(Tick at, std::uint32_t offset) noexcept;
    void   playRange(Tick from, Tick to, const Piece& p) noexcept;
    void   gatherLive(const Block& block) noexcept;
    void   recordLive(const Piece& p) noexcept;
    void   processPiece(Piece& p) noexcept;
    void   startTake(Tick at, bool fromFirstNote, Tick recordFrom) noexcept;
    void   record(Tick tick, const uw::MidiMessage& m) noexcept;
    void   endTake(Tick at) noexcept;
    void   runSynth(const Block& block) noexcept;
    void   adoptPlayData() noexcept;

    uw::HostedPlugin* synth      = nullptr;   // main thread: the latest given
    std::vector<Effect> effects;               // main thread: the latest given (NE)
    std::map<uw::HostedPlugin*, int> activeCount;   // main thread: slots holding each active plugin
    bool              isActive   = false;
    double            sampleRate = 48000.0;
    std::uint32_t     maxFrames  = 0;

    // The synth's slots: `running` is the audio thread's while active (the
    // main thread's otherwise); `latest` the main thread's newest. New ones
    // go to the audio thread through slotIn and old ones come back through
    // slotOut; `slots` owns them all (main thread).
    std::vector<std::unique_ptr<SynthSlot>> slots;
    SynthSlot*                              running = nullptr;
    SynthSlot*                              latest  = nullptr;
    uw::SpscQueue<SynthSlot*, 8>            slotIn, slotOut;
    std::size_t                             slotsInFlight = 0;   // main thread: given, not yet back
    std::vector<uw::HostedPlugin*>          retired;              // main thread: deactivated, for the caller

    std::atomic<bool> offlineMode{false};
    std::atomic<RecordMode> recordModeNow{RecordMode::Host};

    // Live input: MIDI thread -> audio thread, gathered each block into
    // liveBlock (offset and message, MIDI 2.0) before the pieces are played.
    uw::SpscQueue<uw::TimedMidiMessage, 1024> live;
    uw::ump::UpconvertState                    upconverter;   // audio thread
    std::bitset<kKeys>                         liveHeld;      // audio thread: live notes on, for a take's end
    std::vector<Out>                           liveBlock;
    std::size_t                                liveCount = 0, liveNext = 0;

    // The grid's notes (HE): main thread -> audio thread. The grid sends each
    // note's off itself (GridModel::stopHearing), on a channel of its own.
    uw::SpscQueue<uw::MidiMessage, 64> heard;
    void                               playHeard() noexcept;

    // What plays: lane side -> audio thread, and back to be freed.
    uw::SpscQueue<PlayData*, 16> toAudio;
    uw::SpscQueue<PlayData*, 16> toMain;
    std::vector<std::unique_ptr<PlayData>> owned;     // lane side: every PlayData not yet freed
    std::unique_ptr<PlayData>              unsent;    // lane side: the latest, if toAudio was full
    PlayData*                              playing = nullptr;   // audio thread

    // Playback state (audio thread).
    bool               wasPlaying  = false;
    bool               wasRolling  = false;
    bool               dataChanged = false;
    std::int64_t       steadyFrames = 0;
    Tick               expectedTick = 0;
    std::bitset<kKeys> sounding;
    std::vector<int>   chaseIndex;   // per key, scratch for chase()

    // Takes: written by the audio thread while kTakeRecording, read by the
    // lane side once kTakeReady.
    TakeBuffer                 takes[2];
    int                        recordingSlot = -1;   // audio thread
    bool                       takeBlocked = false;  // audio thread: this pass couldn't get a buffer
    std::uint64_t              nextSerial = 1;       // audio thread
    std::atomic<bool>          recordingNow{false};
    std::atomic<std::uint64_t> missedTakes{0};

    // Per-block scratch, sized at activate().
    std::vector<Out>                outEvents;
    std::size_t                     outCount = 0;
    std::vector<uw::Event>          synthEvents;
    std::vector<uw::Event>          chainEvents;   // NE: the effects' input
    bool                            notesOffDue = false;   // audio thread: allNotesOff() at the next block

    // Stats (audio thread writes, main thread reads and clears).
    std::atomic<std::uint64_t> statCount{0}, statClamped{0}, statDropped{0};
    std::atomic<std::int64_t>  statSumNs{0}, statMinNs{INT64_MAX}, statMaxNs{0};
    std::atomic<double>        seenTempo{120.0};
    std::atomic<int>           seenBeatsPerBar{4};
    std::atomic<Tick>          seenPosition{0};
    std::atomic<bool>          seenPlaying{false};
    // Audio thread: where the host last stood stopped (a count-in's end).
    Tick                       stoppedAt   = 0;
    bool                       stoppedSeen = false;
    // How the last take on play began (audio thread writes; the log reads).
    std::atomic<std::uint64_t> startSerial{0};
    std::atomic<Tick>          startPlayFrom{0}, startStoppedAt{0}, startRecordFrom{0};
    std::atomic<bool>          startStoppedSeen{false};
};

} // namespace umpire::plugin
