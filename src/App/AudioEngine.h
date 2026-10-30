#pragma once

// CLAP instruments as track outputs (milestone 8, docs/plugins.md): a
// track's clips can play into a hosted CLAP plugin instead of a MIDI device
// (App/Instruments.h finds them; this hosts them), mixed into the
// platform's audio output through uw-core's audio seam (JACK on Linux,
// CoreAudio on macOS: App/PlatformAudioBackend.h). Each track with a plugin
// output gets its own instance (a "voice"), so one track's plugin never
// hears another's notes and a mute silences only its own.
//
// Player schedules a plugin voice exactly as it schedules a MIDI device
// (MidiOutputs' shape: schedules/protocol/sendAt/cancel), a little ahead on
// uw-core's clock. Each voice keeps what's pending in its own lock-free
// queue (the main thread pushes; the audio thread drains) and, every block,
// turns each event due by the block's end into its exact sample offset from
// the block's start (AudioCallbackContext::blockStartTime) before handing
// it to the plugin's process() — sample-accurate timing survives the hop
// from nanoseconds to samples, as it does from uw-core's clock to ALSA's
// scheduled output (docs/playback.md).
//
// Offline-capable: renderBlock() is the entire audio-thread path and never
// touches the backend; process() (the real-time callback) is a one-line
// forward to it. A future offline render (decisions.md, "Audio: printing,
// mixdown, playback") drives the same voices through renderBlock() with its
// own context and clock, without ever opening a device.
//
// Removing a voice follows HostedPlugin's own contract: the engine calls
// stopProcessing() on the audio thread (inside renderBlock(), the next time
// it runs) before the main thread may deactivate it; close() asks for that
// and waits for it to happen (bounded; immediate if the engine was never
// started, since then nothing else ever calls renderBlock()).

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "App/Instruments.h"
#include "uw/audio/AudioBackend.h"
#include "uw/base/Event.h"
#include "uw/plugin/PluginFactory.h"
#include "uw/ump/MidiMessage.h"

namespace umpire {

using VoiceHandle = std::uint32_t;
inline constexpr VoiceHandle kInvalidVoice = 0;

class AudioEngine final : public uw::AudioProcessor {
public:
    // The most frames ever activated for: PipeWire's documented largest
    // graph buffer (StageHand's docs/architecture.md, "Engine consequence").
    // Block splitting for a backend that asks for more is a safety net, not
    // built yet (docs/plugins.md, "Not in it").
    static constexpr std::uint32_t kMaxBlockFrames    = 8192;
    static constexpr std::size_t   kMaxEventsPerBlock = 256;

    // `backend` may be null (no audio backend on this machine): voices still
    // host and process, with nothing to mix their audio into. Owns neither
    // `backend` nor `factory`.
    AudioEngine(uw::AudioBackend* backend, uw::PluginFactory& factory);
    ~AudioEngine() override;

    AudioEngine(const AudioEngine&)            = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Opens the backend at its defaults and starts it. False (silence; a
    // voice can still be opened and driven through renderBlock(), offline
    // style) if there's no backend or it refused.
    bool start();
    void stop();
    bool running() const noexcept { return started; }

    Instruments&       instruments() noexcept { return instrumentList; }
    const Instruments& instruments() const noexcept { return instrumentList; }

    // A new instance of the plugin `outputId` names (Instruments::outputId),
    // or kInvalidVoice if it isn't a known plugin, or it won't load or
    // activate.
    VoiceHandle open(const std::string& outputId);
    void        close(VoiceHandle voice);
    bool        connected(VoiceHandle voice) const;

    // Mirrors MidiOutputs' shape, so Player can treat a voice like a MIDI
    // stream (App/Player.cpp): a voice's queue holds what's sent to its
    // time, as a scheduling MIDI device does.
    bool             schedules(VoiceHandle voice) const;
    uw::MidiProtocol protocol(VoiceHandle voice) const;
    void             sendAt(VoiceHandle voice, const uw::MidiMessage* messages, std::size_t count,
                            uw::Nanoseconds when);
    void             cancel(VoiceHandle voice);

    // The plugin's name, for a track's row ("No output" is Player's own, not
    // this). `connected` false if it's no longer found (uninstalled).
    std::string label(const std::string& outputId, bool* connected = nullptr) const;

    // Main-thread housekeeping (CLAP's on_main_thread): call every tick.
    void idle();

    const char* backendName() const noexcept { return backend != nullptr ? backend->name() : "no audio"; }

    // --- uw::AudioProcessor: the real-time callback. The only place this
    // object touches the backend's buffers; it forwards to renderBlock()
    // (the class comment, "Offline-capable").
    void process(const uw::AudioCallbackContext& ctx) noexcept override;

    // The audio-thread path: mixes every voice's plugin into ctx.output
    // (summed; no gain or pan yet, decisions.md milestone 14). Never touches
    // the backend, so it runs the same way whether or not one is open.
    void renderBlock(const uw::AudioCallbackContext& ctx) noexcept;

private:
    struct PendingEvent {
        uw::Nanoseconds when       = 0;
        uw::MidiMessage message{};
        std::uint32_t   generation = 0;
    };
    static constexpr std::size_t kQueueCapacity = 256;   // power of two, for SpscQueue

    class VoiceHost;
    struct Voice;

    void             activate(Voice& voice, double sampleRate);
    static uw::Event toClapEvent(const PendingEvent& e, const uw::AudioCallbackContext& ctx, int notePort);

    uw::AudioBackend*                             backend;
    uw::PluginFactory&                            factory;
    Instruments                                   instrumentList;
    std::map<VoiceHandle, std::unique_ptr<Voice>> voices;
    VoiceHandle                                   nextHandle     = 1;
    double                                         liveSampleRate = 48000.0;   // the backend's, once started
    bool                                           started        = false;
};

} // namespace umpire
