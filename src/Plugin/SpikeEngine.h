#pragma once

// Umpire as a plugin, the spike (milestone P1, docs/plugin-version.md): the
// audio-thread side, with no CLAP in it, so the tests drive it as the host
// would. The plugin (Plugin/UmpirePlugin.cpp) is the CLAP glue around it.
//
// What it does each block, in the host's process() call:
//   - takes live input from the MIDI thread (the plugin's own uw-core input,
//     never the host's MIDI) through a SpscQueue, places each message at a
//     sample offset from its capture time, and plays it through to the synth;
//   - records it into a take while the host records (CLAP_TRANSPORT_IS_RECORDING),
//     in ticks from the host's song position (song_pos_beats);
//   - plays the clip back from the host's position, ending notes and chasing
//     Pitch 7.25 when the position jumps (a locate, a loop wrap), as
//     App/Player does on a seek;
//   - runs the hosted synth (a uw::HostedPlugin, uw-core's CLAP host) in the
//     same block, its events at their sample offsets, in MIDI 2.0 where its
//     note port takes it, MIDI 1.0 otherwise (as App/AudioEngine).
//
// Threads: the main thread owns the clip (setClip(), collectTake()); the
// audio thread never allocates or locks. A finished take crosses to the main
// thread through a flag; a new clip crosses to the audio thread, and the old
// one back to be freed, through SpscQueues.
//
// The clip starts at the host's beat 0: its ticks are the song's. Recording
// replaces it (the spike keeps one clip); while recording, it doesn't play.

#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "App/Clip.h"
#include "uw/base/SpscQueue.h"
#include "uw/midi/MidiInput.h"
#include "uw/plugin/HostedPlugin.h"
#include "uw/ump/Ump.h"

namespace umpire::plugin {

// The host's transport at a block's first frame (CLAP's clap_event_transport).
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

// One block, as the host gives it.
struct Block {
    std::uint32_t   frames = 0;
    HostTransport   transport;
    uw::Nanoseconds now = 0;                  // when process() was called, on uw-core's clock
    float* const*   out         = nullptr;    // the host's output channels
    std::uint32_t   outChannels = 0;
};

// What the live input's timing looked like, since the last call to takeStats()
// (docs/plugin-version.md, "Timing": the spike measures it).
struct LiveStats {
    std::uint64_t count   = 0;
    double        meanMs  = 0;   // capture to the block that plays it
    double        minMs   = 0;
    double        maxMs   = 0;
    std::uint64_t clamped = 0;   // older than a block: played at offset 0
    std::uint64_t dropped = 0;   // the queue or the block's events were full
};

class SpikeEngine final : public uw::MidiSink {
public:
    static constexpr std::size_t kMaxEvents     = 2048;      // to the synth in one block (uw::ClapPlugin's limit)
    static constexpr std::size_t kMaxTakeEvents = 1u << 16;  // in one take
    static constexpr int         kKeys          = 16 * 16 * 128;   // group, channel, note

    SpikeEngine();
    ~SpikeEngine() override;

    SpikeEngine(const SpikeEngine&)            = delete;
    SpikeEngine& operator=(const SpikeEngine&) = delete;

    // --- main thread ---

    // The synth the engine plays (null: none, silence). Not owned. Set while
    // inactive.
    void setSynth(uw::HostedPlugin* plugin) { synth = plugin; }
    // Allocates for blocks of up to `maxFrames` and activates the synth.
    // False only if the synth wouldn't activate (the engine still runs, silent).
    bool activate(double sampleRate, std::uint32_t maxFrames);
    void deactivate();
    bool active() const noexcept { return isActive; }
    // What the synth's note port takes, once active: MIDI 2.0, or only MIDI 1.0
    // (then it gets the clip downconverted, without per-note pitch).
    bool synthTakesMidi2() const noexcept { return synthMidi2; }
    bool synthHasNotes() const noexcept { return synthNotePort >= 0; }

    // The render extension: offline, live input is ignored.
    void setOffline(bool offline) { offlineMode.store(offline, std::memory_order_relaxed); }
    bool offline() const { return offlineMode.load(std::memory_order_relaxed); }

    // Called on the audio thread when a take ends, so the main thread can
    // collect it soon (CLAP host request_callback; any thread). Optional.
    void setNotify(void (*fn)(void*), void* context) {
        notifyFn  = fn;
        notifyCtx = context;
    }

    // The clip the audio thread plays from now on (a copy is taken).
    void setClip(const Clip& clip);
    // A take that has ended since the last call, as a clip: false if none.
    // `start`, if given, gets the tick recording began at (for the log).
    bool collectTake(Clip& out, Tick* start = nullptr);
    // Frees clips the audio thread has let go of. Call now and then.
    void collectGarbage();

    LiveStats takeStats();
    // The host's tempo and meter, last seen (for writing a clip file).
    double lastTempo() const { return seenTempo.load(std::memory_order_relaxed); }
    int    lastBeatsPerBar() const { return seenBeatsPerBar.load(std::memory_order_relaxed); }
    bool   recording() const { return takeState.load(std::memory_order_relaxed) == kTakeRecording; }

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
    enum : int { kTakeIdle = 0, kTakeRecording = 1, kTakeReady = 2 };

    void   emit(std::uint32_t offset, const uw::MidiMessage& m) noexcept;
    void   endPlayNotes() noexcept;
    void   chase(Tick at) noexcept;
    void   playRange(Tick from, Tick to, Tick blockTick, double ticksPerFrame, std::uint32_t frames) noexcept;
    void   takeLive(const Block& block, Tick blockTick, double ticksPerFrame) noexcept;
    void   record(Tick tick, const uw::MidiMessage& m) noexcept;
    void   endTake(Tick at) noexcept;
    void   runSynth(const Block& block) noexcept;
    void   adoptPlayData() noexcept;

    uw::HostedPlugin* synth      = nullptr;
    bool              isActive   = false;
    double            sampleRate = 48000.0;
    std::uint32_t     maxFrames  = 0;
    bool              synthActive = false;
    bool              synthMidi2 = true;
    int               synthNotePort = -1;
    std::uint32_t     synthChannels = 0;

    std::atomic<bool> offlineMode{false};
    void (*notifyFn)(void*) = nullptr;
    void* notifyCtx         = nullptr;

    // Live input: MIDI thread -> audio thread.
    uw::SpscQueue<uw::TimedMidiMessage, 1024> live;
    uw::ump::UpconvertState                    upconverter;   // audio thread
    std::bitset<kKeys>                         liveHeld;      // audio thread: live notes on, for a take's end

    // The clip: main thread -> audio thread, and back to be freed.
    uw::SpscQueue<PlayData*, 16> toAudio;
    uw::SpscQueue<PlayData*, 16> toMain;
    std::vector<std::unique_ptr<PlayData>> owned;   // main thread: every PlayData not yet freed
    PlayData*                              playing = nullptr;   // audio thread

    // Playback state (audio thread).
    bool               wasPlaying  = false;
    bool               clipJumped  = false;
    std::int64_t       steadyFrames = 0;
    Tick               expectedTick = 0;
    std::bitset<kKeys> sounding;
    std::vector<int>   chaseIndex;   // per key, scratch for chase()

    // The take: written by the audio thread while kTakeRecording, read by the
    // main thread once kTakeReady.
    std::atomic<int>       takeState{kTakeIdle};
    std::vector<ClipEvent> takeEvents;
    std::size_t            takeCount = 0;
    Tick                   takeStart = 0, takeEnd = 0;
    bool                   takeBlocked = false;   // the last take wasn't collected yet

    // Per-block scratch, sized at activate().
    std::vector<Out>                outEvents;
    std::size_t                     outCount = 0;
    std::vector<uw::Event>          synthEvents;
    std::vector<std::vector<float>> scratch;
    std::vector<float*>             scratchPointers;

    // Stats (audio thread writes, main thread reads and clears).
    std::atomic<std::uint64_t> statCount{0}, statClamped{0}, statDropped{0};
    std::atomic<std::int64_t>  statSumNs{0}, statMinNs{INT64_MAX}, statMaxNs{0};
    std::atomic<double>        seenTempo{120.0};
    std::atomic<int>           seenBeatsPerBar{4};
};

} // namespace umpire::plugin
