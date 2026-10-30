#include "App/AudioEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <thread>

#include "uw/base/SpscQueue.h"
#include "uw/plugin/HostServices.h"
#include "uw/plugin/HostedPlugin.h"
#include "uw/plugin/PortLayout.h"

namespace umpire {

namespace {
uw::HostInfo umpireHostInfo() { return {"Umpire", "Unlikelyware", "https://unlikelyware.com", "0.0.1"}; }
} // namespace

// The HostServices one voice's plugin sees: logging and identity are the
// engine's; the rest aren't acted on yet (no plugin editors or param UI
// before milestone 11, so a plugin asking to be restarted or flushed has
// nothing to disturb).
class AudioEngine::VoiceHost final : public uw::HostServices {
public:
    explicit VoiceHost(std::string pluginName) : name(std::move(pluginName)) {}

    void requestRestart() override {}
    void requestProcess() override {}
    void requestParamFlush() override {}
    void log(uw::LogLevel level, const char* message) override {
        if (level >= uw::LogLevel::Warning) std::fprintf(stderr, "[%s] %s\n", name.c_str(), message);
    }
    uw::HostInfo hostInfo() const override { return umpireHostInfo(); }

private:
    std::string name;
};

// One track's plugin instance and what it needs to process a block.
struct AudioEngine::Voice {
    std::string                             pluginId;
    std::unique_ptr<AudioEngine::VoiceHost> host;
    std::unique_ptr<uw::HostedPlugin>       plugin;
    bool                                    activated    = false;
    int                                     notePort     = -1;
    uw::NoteDialectMask                     noteDialect  = uw::NoteDialect::Midi2;
    int                                     mainOutput   = -1;
    std::uint32_t                           channelCount = 0;

    uw::SpscQueue<AudioEngine::PendingEvent, AudioEngine::kQueueCapacity> queue;
    std::atomic<std::uint32_t>                                           generation{0};
    std::atomic<bool>                                                    removeRequested{false};
    std::atomic<bool>                                                    retired{false};

    // Render scratch, sized at activate() so the audio thread never
    // allocates: planar, kMaxBlockFrames long per channel.
    std::vector<std::vector<float>> scratch;
    std::vector<float*>             scratchPointers;
    std::vector<uw::Event>          blockEvents;   // reused each block
};

AudioEngine::AudioEngine(uw::AudioBackend* b, uw::PluginFactory& f) : backend(b), factory(f), instrumentList(f) {}

AudioEngine::~AudioEngine() {
    if (started) stop();
    std::vector<VoiceHandle> handles;
    handles.reserve(voices.size());
    for (const auto& [handle, voice] : voices) handles.push_back(handle);
    for (VoiceHandle h : handles) close(h);
}

void AudioEngine::activate(Voice& voice, double sampleRate) {
    if (voice.activated) return;
    if (!voice.plugin->activate(sampleRate, kMaxBlockFrames)) return;
    voice.activated = true;

    const uw::PortLayout layout = voice.plugin->portLayout();
    voice.notePort     = layout.noteInputs.empty() ? -1 : 0;
    voice.noteDialect  = layout.noteInputs.empty() ? uw::NoteDialect::Midi2 : layout.noteInputs[0].preferredDialect;
    voice.mainOutput   = uw::mainAudioOutput(layout);
    voice.channelCount = voice.mainOutput >= 0
                             ? layout.audioOutputs[static_cast<std::size_t>(voice.mainOutput)].channelCount
                             : 0;

    voice.scratch.assign(voice.channelCount, std::vector<float>(kMaxBlockFrames, 0.0f));
    voice.scratchPointers.resize(voice.channelCount);
    for (std::uint32_t c = 0; c < voice.channelCount; ++c) voice.scratchPointers[c] = voice.scratch[c].data();
    voice.blockEvents.assign(kMaxEventsPerBlock, uw::Event{});
}

bool AudioEngine::start() {
    if (backend == nullptr || started) return false;
    uw::AudioConfig requested;
    requested.outputChannels = 2;
    uw::AudioConfig negotiated;
    if (!backend->open(requested, negotiated)) return false;
    liveSampleRate = negotiated.sampleRate > 0 ? negotiated.sampleRate : 48000.0;

    // Reactivate anything opened before the backend came up, at the real
    // rate. Safe here: the backend hasn't started, so nothing is on the
    // audio thread yet.
    for (auto& [handle, voice] : voices) {
        if (!voice->activated) continue;
        voice->plugin->deactivate();
        voice->activated = false;
        activate(*voice, liveSampleRate);
    }

    backend->setProcessor(this);
    if (!backend->start()) {
        backend->close();
        return false;
    }
    started = true;
    return true;
}

void AudioEngine::stop() {
    if (!started) return;
    backend->stop();
    backend->close();
    started = false;
    for (auto& [handle, voice] : voices)
        if (voice->activated) voice->plugin->stopProcessing();
}

VoiceHandle AudioEngine::open(const std::string& outputId) {
    const uw::PluginDescriptor* desc = instrumentList.find(outputId);
    if (desc == nullptr) return kInvalidVoice;

    auto voice      = std::make_unique<Voice>();
    voice->pluginId = outputId;
    voice->host     = std::make_unique<VoiceHost>(desc->name);
    voice->plugin   = factory.create(*desc, *voice->host);
    if (!voice->plugin) return kInvalidVoice;

    activate(*voice, started ? liveSampleRate : 48000.0);
    if (!voice->activated) return kInvalidVoice;

    const VoiceHandle handle = nextHandle++;
    voices[handle]           = std::move(voice);
    return handle;
}

void AudioEngine::close(VoiceHandle handle) {
    const auto it = voices.find(handle);
    if (it == voices.end()) return;
    Voice& v = *it->second;
    if (v.activated) {
        if (started) {
            v.removeRequested.store(true, std::memory_order_release);
            // The audio thread retires it (calls stopProcessing()) the next
            // time renderBlock() runs; bounded, since that's every block.
            for (int spins = 0; spins < 2000 && !v.retired.load(std::memory_order_acquire); ++spins)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else {
            v.plugin->stopProcessing();   // no-op if it never processed
        }
        v.plugin->deactivate();
    }
    voices.erase(it);
}

bool AudioEngine::connected(VoiceHandle handle) const {
    const auto it = voices.find(handle);
    return it != voices.end() && it->second->activated;
}

bool AudioEngine::schedules(VoiceHandle handle) const { return voices.count(handle) != 0; }

uw::MidiProtocol AudioEngine::protocol(VoiceHandle handle) const {
    const auto it = voices.find(handle);
    if (it == voices.end()) return uw::MidiProtocol::Midi1;
    return (it->second->noteDialect & uw::NoteDialect::Midi2) != 0 ? uw::MidiProtocol::Midi2 : uw::MidiProtocol::Midi1;
}

void AudioEngine::sendAt(VoiceHandle handle, const uw::MidiMessage* messages, std::size_t count,
                         uw::Nanoseconds when) {
    const auto it = voices.find(handle);
    if (it == voices.end() || it->second->notePort < 0) return;
    Voice&               v   = *it->second;
    const std::uint32_t gen = v.generation.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < count; ++i) v.queue.push(PendingEvent{when, messages[i], gen});
}

void AudioEngine::cancel(VoiceHandle handle) {
    const auto it = voices.find(handle);
    if (it == voices.end()) return;
    it->second->generation.fetch_add(1, std::memory_order_relaxed);
}

std::string AudioEngine::label(const std::string& outputId, bool* connectedOut) const {
    const uw::PluginDescriptor* desc = instrumentList.find(outputId);
    if (connectedOut != nullptr) *connectedOut = desc != nullptr;
    return desc != nullptr ? desc->name : outputId;
}

void AudioEngine::idle() {
    for (auto& [handle, voice] : voices)
        if (voice->activated) voice->plugin->idle();
}

uw::Event AudioEngine::toClapEvent(const PendingEvent& e, const uw::AudioCallbackContext& ctx, int notePort) {
    uw::Event out;
    out.type      = uw::EventType::Midi;
    out.midi      = e.message;
    out.portIndex = static_cast<std::uint16_t>(notePort < 0 ? 0 : notePort);
    const double offset = static_cast<double>(e.when - ctx.blockStartTime) * ctx.time.sampleRate / 1e9;
    std::int64_t sample = offset < 0.0 ? 0 : static_cast<std::int64_t>(offset + 0.5);
    if (ctx.frameCount > 0 && sample >= static_cast<std::int64_t>(ctx.frameCount))
        sample = static_cast<std::int64_t>(ctx.frameCount) - 1;
    out.sampleOffset = static_cast<std::uint32_t>(std::max<std::int64_t>(0, sample));
    return out;
}

void AudioEngine::process(const uw::AudioCallbackContext& ctx) noexcept { renderBlock(ctx); }

void AudioEngine::renderBlock(const uw::AudioCallbackContext& ctx) noexcept {
    for (std::uint32_t c = 0; c < ctx.output.channelCount; ++c)
        for (std::uint32_t f = 0; f < ctx.frameCount; ++f) ctx.output.channels[c][f] = 0.0f;

    const auto blockEnd =
        ctx.blockStartTime + static_cast<uw::Nanoseconds>(static_cast<double>(ctx.frameCount) / ctx.time.sampleRate * 1e9);

    for (auto& [handle, voicePtr] : voices) {
        Voice& v = *voicePtr;
        if (!v.activated) continue;
        if (v.removeRequested.load(std::memory_order_acquire)) {
            v.plugin->stopProcessing();
            v.retired.store(true, std::memory_order_release);
            continue;
        }

        std::size_t          eventCount = 0;
        const std::uint32_t gen        = v.generation.load(std::memory_order_acquire);
        while (eventCount < kMaxEventsPerBlock) {
            const PendingEvent* next = v.queue.peek();
            if (next == nullptr) break;
            if (next->generation != gen) {   // cancelled since it was sent
                PendingEvent discard;
                v.queue.pop(discard);
                continue;
            }
            if (next->when >= blockEnd) break;   // not due this block
            v.blockEvents[eventCount++] = toClapEvent(*next, ctx, v.notePort);
            PendingEvent discard;
            v.queue.pop(discard);
        }
        std::stable_sort(v.blockEvents.begin(), std::next(v.blockEvents.begin(), static_cast<std::ptrdiff_t>(eventCount)),
                         [](const uw::Event& a, const uw::Event& b) { return a.sampleOffset < b.sampleOffset; });

        for (std::uint32_t c = 0; c < v.channelCount; ++c)
            for (std::uint32_t f = 0; f < ctx.frameCount; ++f) v.scratchPointers[c][f] = 0.0f;

        uw::ProcessContext pctx;
        pctx.frameCount = ctx.frameCount;
        pctx.time       = ctx.time;
        pctx.audioOut   = uw::AudioBuffer{v.scratchPointers.data(), v.channelCount, ctx.frameCount};
        pctx.eventsIn   = uw::EventBlock{v.blockEvents.data(), static_cast<std::uint32_t>(eventCount)};
        v.plugin->process(pctx);

        const std::uint32_t mixChannels = std::min(v.channelCount, ctx.output.channelCount);
        for (std::uint32_t c = 0; c < mixChannels; ++c)
            for (std::uint32_t f = 0; f < ctx.frameCount; ++f) ctx.output.channels[c][f] += v.scratchPointers[c][f];
    }
}

} // namespace umpire
