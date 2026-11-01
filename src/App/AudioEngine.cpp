#include "App/AudioEngine.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <thread>

#include "uw/base/SpscQueue.h"
#include "uw/plugin/EditorWindow.h"
#include "uw/plugin/HostServices.h"
#include "uw/plugin/HostedPlugin.h"
#include "uw/plugin/PortLayout.h"

namespace umpire {

namespace {
uw::HostInfo umpireHostInfo() { return {"Umpire", "Unlikelyware", "https://unlikelyware.com", "0.0.1"}; }
} // namespace

// The HostServices one voice's plugin sees: logging and identity are the
// engine's; the loop services (milestone 11) forward to the app's MainLoop,
// since a plugin editor has no event loop of its own (CLAP's timer-support
// and posix-fd-support). The rest aren't acted on yet: no param UI before a
// plugin's parameters are exposed, so a flush request has nothing to apply.
class AudioEngine::VoiceHost final : public uw::HostServices {
public:
    VoiceHost(std::string pluginName, MainLoop& mainLoop) : name(std::move(pluginName)), loop(mainLoop) {}

    void requestRestart() override {}
    void requestProcess() override {}
    void requestParamFlush() override {}
    void log(uw::LogLevel level, const char* message) override {
        if (level >= uw::LogLevel::Warning) std::fprintf(stderr, "[%s] %s\n", name.c_str(), message);
    }
    uw::HostInfo hostInfo() const override { return umpireHostInfo(); }

    uw::TimerId addTimer(std::uint32_t periodMs, std::function<void()> callback) override {
        return loop.addTimer(periodMs, std::move(callback));
    }
    void removeTimer(uw::TimerId id) override { loop.removeTimer(id); }
    bool watchFd(int fd, std::uint32_t events, std::function<void(std::uint32_t)> callback) override {
        return loop.watchFd(fd, events, std::move(callback));
    }
    bool modifyFd(int fd, std::uint32_t events) override { return loop.modifyFd(fd, events); }
    void unwatchFd(int fd) override { loop.unwatchFd(fd); }

private:
    std::string name;
    MainLoop&   loop;
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

    // The window hosting this voice's plugin editor, if one is open
    // (milestone 11). Main thread only; closed before the plugin deactivates.
    std::unique_ptr<uw::EditorWindow> editor;
};

AudioEngine::AudioEngine(uw::AudioBackend* b, uw::PluginFactory& f, MainLoop& loop)
    : backend(b), factory(f), mainLoop(loop), instrumentList(f) {}

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

VoiceHandle AudioEngine::open(const std::string& outputId, const std::vector<std::byte>* state) {
    const uw::PluginDescriptor* desc = instrumentList.find(outputId);
    if (desc == nullptr) return kInvalidVoice;

    auto voice      = std::make_unique<Voice>();
    voice->pluginId = outputId;
    voice->host     = std::make_unique<VoiceHost>(desc->name, mainLoop);
    voice->plugin   = factory.create(*desc, *voice->host);
    if (!voice->plugin) return kInvalidVoice;
    // Before activate(), as HostedPlugin asks. A state it refuses leaves it
    // at its defaults, which is what it would have had anyway.
    if (state != nullptr && !state->empty() && !voice->plugin->loadState(state->data(), state->size()))
        std::fprintf(stderr, "%s: couldn't restore its saved state\n", desc->name.c_str());

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
    v.editor.reset();   // the editor closes before its plugin does (HostedPlugin.h)
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

bool AudioEngine::saveState(VoiceHandle handle, std::vector<std::byte>& out) const {
    const auto it = voices.find(handle);
    if (it == voices.end() || !it->second->plugin) return false;
    out.clear();
    return it->second->plugin->saveState(out);
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

bool AudioEngine::openEditor(VoiceHandle handle, const std::string& title, std::function<void()> requestQuit) {
    const auto it = voices.find(handle);
    if (it == voices.end() || !it->second->activated) return false;
    Voice& v = *it->second;
    if (v.editor && v.editor->isOpen()) return true;   // already open: nothing to do
    v.editor = uw::EditorWindow::open(*v.plugin, *v.host, title, std::move(requestQuit));
    return v.editor != nullptr;
}

void AudioEngine::closeEditor(VoiceHandle handle) {
    const auto it = voices.find(handle);
    if (it != voices.end()) it->second->editor.reset();
}

bool AudioEngine::editorOpen(VoiceHandle handle) const {
    const auto it = voices.find(handle);
    return it != voices.end() && it->second->editor && it->second->editor->isOpen();
}

void AudioEngine::idle() {
    for (auto& [handle, voice] : voices) {
        if (voice->activated) voice->plugin->idle();
        // The user (or the plugin) closed the window on its own: drop it, so
        // editorOpen() reports it gone and a later openEditor() makes a
        // fresh one rather than reusing a closed EditorWindow.
        if (voice->editor && !voice->editor->isOpen()) voice->editor.reset();
    }
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
