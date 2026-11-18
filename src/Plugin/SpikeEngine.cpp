#include "Plugin/SpikeEngine.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>

namespace umpire::plugin {

namespace ump = uw::ump;

namespace {

int noteKey(const uw::MidiMessage& m) {
    return (ump::group(m) * 16 + ump::channel(m)) * 128 + ump::noteNumber(m);
}

uw::MidiMessage noteOff(int key) {
    return ump::midi2NoteOff(static_cast<std::uint8_t>(key / 128 / 16), static_cast<std::uint8_t>(key / 128 % 16),
                             static_cast<std::uint8_t>(key % 128), 0);
}

bool isMidi2(const uw::MidiMessage& m) { return ump::messageType(m) == ump::MessageType::Midi2ChannelVoice; }

// Pitch 7.25: MIDI 2.0 registered per-note controller 3 (as App/Player.cpp).
bool isPitch725(const uw::MidiMessage& m) {
    return isMidi2(m) && ump::status(m) == ump::Status::RegisteredPerNoteController && (m.words[0] & 0xFF) == 3;
}

// Per-Note Management with its reset flag: the key's per-note controllers
// go back to their defaults.
bool isReset(const uw::MidiMessage& m) {
    return isMidi2(m) && ump::status(m) == ump::Status::PerNoteManagement && (m.words[0] & 0x01) != 0;
}

uw::MidiMessage resetKey(int key) {
    uw::MidiMessage m;
    m.words[0] = 0x40000000u | static_cast<std::uint32_t>(key / 128 / 16) << 24 | 0xFu << 20 |
                 static_cast<std::uint32_t>(key / 128 % 16) << 16 | static_cast<std::uint32_t>(key % 128) << 8 | 0x01;
    m.wordCount = 2;
    return m;
}

void atomicMin(std::atomic<std::int64_t>& a, std::int64_t v) {
    std::int64_t cur = a.load(std::memory_order_relaxed);
    while (v < cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}
void atomicMax(std::atomic<std::int64_t>& a, std::int64_t v) {
    std::int64_t cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}

} // namespace

SpikeEngine::SpikeEngine() {
    takeEvents.resize(kMaxTakeEvents);
    chaseIndex.assign(kKeys, -1);
    outEvents.resize(kMaxEvents);
    synthEvents.resize(kMaxEvents);
}

SpikeEngine::~SpikeEngine() = default;   // `owned` frees every clip, the one playing included

// --- main thread -----------------------------------------------------------------------

bool SpikeEngine::activate(double rate, std::uint32_t frames) {
    sampleRate = rate > 0 ? rate : 48000.0;
    maxFrames  = frames;
    synthActive = false;
    if (synth != nullptr && synth->activate(sampleRate, maxFrames)) {
        synthActive                  = true;
        const uw::PortLayout layout  = synth->portLayout();
        synthNotePort                = layout.noteInputs.empty() ? -1 : 0;
        synthMidi2    = !layout.noteInputs.empty() && (layout.noteInputs[0].supportedDialects & uw::NoteDialect::Midi2) != 0;
        const int main = uw::mainAudioOutput(layout);
        synthChannels  = main >= 0 ? layout.audioOutputs[static_cast<std::size_t>(main)].channelCount : 0;
    } else {
        synthNotePort = -1;
        synthChannels = 0;
    }
    scratch.assign(synthChannels, std::vector<float>(maxFrames, 0.0f));
    scratchPointers.resize(synthChannels);
    for (std::uint32_t c = 0; c < synthChannels; ++c) scratchPointers[c] = scratch[c].data();
    wasPlaying = false;
    sounding.reset();
    isActive = true;
    return synth == nullptr || synthActive;
}

void SpikeEngine::deactivate() {
    if (!isActive) return;
    if (synthActive) synth->deactivate();
    synthActive = false;
    isActive    = false;
}

void SpikeEngine::setClip(const Clip& clip) {
    auto data = std::make_unique<PlayData>();
    std::set<int> tuned;
    for (const ClipEvent& e : clip.events) {
        if (!ump::isChannelVoice(e.message)) continue;
        data->events.push_back(e);
        if (isPitch725(e.message)) tuned.insert(noteKey(e.message));
        if (isPitch725(e.message) || isReset(e.message)) data->tunings.push_back(e);
    }
    const auto byTick = [](const ClipEvent& a, const ClipEvent& b) { return a.tick < b.tick; };
    std::stable_sort(data->events.begin(), data->events.end(), byTick);
    std::stable_sort(data->tunings.begin(), data->tunings.end(), byTick);
    data->tunedKeys.assign(tuned.begin(), tuned.end());

    collectGarbage();
    if (!toAudio.push(data.get())) return;   // sixteen clips waiting: the audio thread isn't running
    owned.push_back(std::move(data));
}

bool SpikeEngine::collectTake(Clip& out) {
    if (takeState.load(std::memory_order_acquire) != kTakeReady) return false;
    out        = Clip{};
    out.name   = "Take";
    out.length = takeEnd;
    out.events.assign(takeEvents.begin(), std::next(takeEvents.begin(), static_cast<std::ptrdiff_t>(takeCount)));
    std::stable_sort(out.events.begin(), out.events.end(),
                     [](const ClipEvent& a, const ClipEvent& b) { return a.tick < b.tick; });
    takeState.store(kTakeIdle, std::memory_order_release);
    return true;
}

void SpikeEngine::collectGarbage() {
    PlayData* done = nullptr;
    while (toMain.pop(done))
        owned.erase(std::remove_if(owned.begin(), owned.end(), [&](const auto& p) { return p.get() == done; }),
                    owned.end());
}

LiveStats SpikeEngine::takeStats() {
    LiveStats s;
    s.count            = statCount.exchange(0);
    const auto sum     = statSumNs.exchange(0);
    const auto lowest  = statMinNs.exchange(INT64_MAX);
    const auto highest = statMaxNs.exchange(0);
    s.clamped          = statClamped.exchange(0);
    s.dropped          = statDropped.exchange(0);
    if (s.count > 0) {
        s.meanMs = static_cast<double>(sum) / static_cast<double>(s.count) / 1e6;
        s.minMs  = static_cast<double>(lowest) / 1e6;
        s.maxMs  = static_cast<double>(highest) / 1e6;
    }
    return s;
}

// --- MIDI thread -----------------------------------------------------------------------

void SpikeEngine::onMidi(uw::MidiSourceHandle, const uw::TimedMidiMessage& msg) noexcept {
    if (!live.push(msg)) statDropped.fetch_add(1, std::memory_order_relaxed);
}

// --- audio thread ----------------------------------------------------------------------

void SpikeEngine::emit(std::uint32_t offset, const uw::MidiMessage& m) noexcept {
    if (outCount >= outEvents.size()) {
        statDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    outEvents[outCount++] = Out{offset, m};
}

void SpikeEngine::endPlayNotes() noexcept {
    if (sounding.none()) return;
    for (int key = 0; key < kKeys; ++key)
        if (sounding.test(static_cast<std::size_t>(key))) emit(0, noteOff(key));
    sounding.reset();
}

// Each key the clip tunes gets the Pitch 7.25 in force at `at`, or a reset
// where none is (App/Player.cpp's chase), at the block's first frame.
void SpikeEngine::chase(Tick at) noexcept {
    if (playing == nullptr || playing->tunedKeys.empty()) return;
    const auto& tunings = playing->tunings;
    for (std::size_t i = 0; i < tunings.size() && tunings[i].tick < at; ++i)
        chaseIndex[static_cast<std::size_t>(noteKey(tunings[i].message))] = static_cast<int>(i);
    for (const int key : playing->tunedKeys) {
        const int i = chaseIndex[static_cast<std::size_t>(key)];
        emit(0, i >= 0 ? tunings[static_cast<std::size_t>(i)].message : resetKey(key));
    }
    for (std::size_t i = 0; i < tunings.size() && tunings[i].tick < at; ++i)
        chaseIndex[static_cast<std::size_t>(noteKey(tunings[i].message))] = -1;
}

void SpikeEngine::playRange(Tick from, Tick to, Tick blockTick, double ticksPerFrame, std::uint32_t frames) noexcept {
    if (playing == nullptr || to <= from) return;
    const auto& events = playing->events;
    auto it = std::lower_bound(events.begin(), events.end(), from,
                               [](const ClipEvent& e, Tick t) { return e.tick < t; });
    for (; it != events.end() && it->tick < to; ++it) {
        double frame = std::round(static_cast<double>(it->tick - blockTick) / ticksPerFrame);
        frame        = std::clamp(frame, 0.0, static_cast<double>(frames - 1));
        const uw::MidiMessage& m = it->message;
        if (ump::isNoteOn(m)) sounding.set(static_cast<std::size_t>(noteKey(m)));
        else if (ump::isNoteOff(m)) sounding.reset(static_cast<std::size_t>(noteKey(m)));
        emit(static_cast<std::uint32_t>(frame), m);
    }
}

void SpikeEngine::record(Tick tick, const uw::MidiMessage& m) noexcept {
    if (!ump::isChannelVoice(m)) return;
    if (takeCount >= takeEvents.size()) {
        statDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    takeEvents[takeCount++] = ClipEvent{tick, m};
    if (ump::isNoteOn(m)) liveHeld.set(static_cast<std::size_t>(noteKey(m)));
    else if (ump::isNoteOff(m)) liveHeld.reset(static_cast<std::size_t>(noteKey(m)));
}

// Notes still held end with the take, so it never leaves one sounding.
void SpikeEngine::endTake(Tick at) noexcept {
    if (liveHeld.any())
        for (int key = 0; key < kKeys; ++key)
            if (liveHeld.test(static_cast<std::size_t>(key))) record(at, noteOff(key));
    liveHeld.reset();
    takeEnd = std::max(at, takeStart);
    takeState.store(kTakeReady, std::memory_order_release);
    if (notifyFn != nullptr) notifyFn(notifyCtx);
}

// Live input since the last block, each message at a sample offset from its
// capture time: a block's frames are taken to be the block's length of time
// before process() was called, so the delay is about a block, steady, and
// the jitter is the host's callback jitter, not a block's.
void SpikeEngine::takeLive(const Block& b, Tick blockTick, double ticksPerFrame) noexcept {
    uw::TimedMidiMessage timed;
    if (offline()) {   // the render extension: a render never hears what's played now
        while (live.pop(timed)) {}
        return;
    }
    const double          framesPerNs = sampleRate / 1e9;
    const uw::Nanoseconds blockNs     = static_cast<uw::Nanoseconds>(static_cast<double>(b.frames) / framesPerNs);
    const uw::Nanoseconds windowStart = b.now - blockNs;
    const bool            recordingNow = takeState.load(std::memory_order_relaxed) == kTakeRecording;
    while (const uw::TimedMidiMessage* next = live.peek()) {
        if (next->captureTime > b.now) break;   // came in after this block began: the next block's
        live.pop(timed);

        const std::int64_t delay = b.now - timed.captureTime;
        statCount.fetch_add(1, std::memory_order_relaxed);
        statSumNs.fetch_add(delay, std::memory_order_relaxed);
        atomicMin(statMinNs, delay);
        atomicMax(statMaxNs, delay);

        double frame = static_cast<double>(timed.captureTime - windowStart) * framesPerNs;
        if (frame < 0) {
            statClamped.fetch_add(1, std::memory_order_relaxed);
            frame = 0;
        }
        const auto offset = static_cast<std::uint32_t>(std::min(frame, static_cast<double>(b.frames - 1)));

        // MIDI 1.0 is upconverted first, always, as the standalone app does
        // (App/TrackController.cpp), so the clip is MIDI 2.0.
        uw::MidiMessage converted[ump::kMaxTranslatedMessages];
        std::size_t     count = 1;
        converted[0]          = timed.message;
        if (ump::messageType(timed.message) == ump::MessageType::Midi1ChannelVoice)
            count = ump::upconvert(timed.message, upconverter, converted, std::size(converted));
        for (std::size_t i = 0; i < count; ++i) {
            emit(offset, converted[i]);
            if (recordingNow) record(blockTick + std::llround(offset * ticksPerFrame), converted[i]);
        }
    }
}

void SpikeEngine::adoptPlayData() noexcept {
    PlayData* next = nullptr;
    while (toAudio.pop(next)) {
        if (playing != nullptr) toMain.push(playing);
        playing    = next;
        clipJumped = true;   // the old clip's notes end; the new one's tuning is chased
    }
}

void SpikeEngine::process(const Block& b) noexcept {
    if (!isActive || b.frames == 0 || b.frames > maxFrames) {
        for (std::uint32_t c = 0; c < b.outChannels; ++c) std::fill_n(b.out[c], b.frames, 0.0f);
        return;
    }
    outCount = 0;
    adoptPlayData();

    const HostTransport& t = b.transport;
    const double tempo     = t.tempo > 0 ? t.tempo : 120.0;
    seenTempo.store(tempo, std::memory_order_relaxed);
    seenBeatsPerBar.store(t.tsigDen == 4 && t.tsigNum > 0 ? t.tsigNum : 4, std::memory_order_relaxed);

    const double ticksPerFrame = tempo / 60.0 * static_cast<double>(kTicksPerQuarter) / sampleRate;
    const bool   rolling       = t.playing && t.hasBeats;
    const Tick   blockTick     = rolling ? std::llround(t.songBeats * static_cast<double>(kTicksPerQuarter)) : expectedTick;
    const Tick   blockTicks    = std::llround(static_cast<double>(b.frames) * ticksPerFrame);
    const Tick   blockEnd      = blockTick + blockTicks;
    const bool   rec           = rolling && t.recording && !offline();

    // Recording follows the host's record button.
    const int state = takeState.load(std::memory_order_acquire);
    if (rec && state == kTakeIdle && !takeBlocked) {
        takeCount = 0;
        takeStart = blockTick;
        liveHeld.reset();
        takeState.store(kTakeRecording, std::memory_order_relaxed);
    } else if (rec && state == kTakeReady) {
        takeBlocked = true;   // the last take hasn't been collected: this pass isn't recorded
    }
    if (!rec) takeBlocked = false;
    if (!rec && state == kTakeRecording) endTake(blockTick);

    // Playback from the host's position. A jump (a locate, a loop wrap, a
    // new clip) ends the notes sounding and chases the tuning; tempo
    // changes move the position a little against the last block's guess,
    // so only more than half a block counts.
    const bool play = rolling && !rec && playing != nullptr;
    if (wasPlaying && !play) endPlayNotes();
    if (play) {
        const Tick tolerance = blockTicks / 2 + 16;
        const bool jumped    = !wasPlaying || clipJumped || std::llabs(blockTick - expectedTick) > tolerance;
        if (jumped) {
            endPlayNotes();
            chase(blockTick);
        }
        playRange(jumped ? blockTick : expectedTick, blockEnd, blockTick, ticksPerFrame, b.frames);
    } else if (clipJumped) {
        endPlayNotes();
    }
    clipJumped   = false;
    wasPlaying   = play;
    expectedTick = rolling ? blockEnd : blockTick;

    takeLive(b, blockTick, ticksPerFrame);
    runSynth(b);
    steadyFrames += b.frames;
}

void SpikeEngine::runSynth(const Block& b) noexcept {
    // Stable insertion sort by offset: nearly sorted already, and std::stable_sort may allocate.
    for (std::size_t i = 1; i < outCount; ++i) {
        const Out   e = outEvents[i];
        std::size_t j = i;
        for (; j > 0 && outEvents[j - 1].offset > e.offset; --j) outEvents[j] = outEvents[j - 1];
        outEvents[j] = e;
    }

    if (!synthActive) {
        for (std::uint32_t c = 0; c < b.outChannels; ++c) std::fill_n(b.out[c], b.frames, 0.0f);
        return;
    }

    std::size_t n = 0;
    if (synthNotePort >= 0) {
        uw::MidiMessage converted[ump::kMaxTranslatedMessages];
        for (std::size_t i = 0; i < outCount; ++i) {
            const uw::MidiMessage& m = outEvents[i].message;
            std::size_t            count = 1;
            converted[0]                 = m;
            if (!synthMidi2 && isMidi2(m)) count = ump::downconvert(m, converted, std::size(converted));
            for (std::size_t k = 0; k < count && n < synthEvents.size(); ++k) {
                uw::Event& e   = synthEvents[n++];
                e              = uw::Event{};
                e.sampleOffset = outEvents[i].offset;
                e.type         = uw::EventType::Midi;
                e.midi         = converted[k];
                e.portIndex    = static_cast<std::uint16_t>(synthNotePort);
            }
        }
    }

    for (std::uint32_t c = 0; c < synthChannels; ++c) std::fill_n(scratchPointers[c], b.frames, 0.0f);

    const HostTransport& t = b.transport;
    uw::ProcessContext   ctx;
    ctx.frameCount                = b.frames;
    ctx.time.sampleRate           = sampleRate;
    ctx.time.samplePosition       = steadyFrames;
    ctx.time.tempoBpm             = t.tempo > 0 ? t.tempo : 120.0;
    ctx.time.timeSigNumerator     = t.tsigNum;
    ctx.time.timeSigDenominator   = t.tsigDen;
    ctx.time.playing              = t.playing;
    ctx.time.beatPosition         = t.songBeats;
    ctx.time.barStartBeats        = t.barStartBeats;
    ctx.time.barNumber            = t.barNumber;
    ctx.audioOut = uw::AudioBuffer{scratchPointers.data(), synthChannels, b.frames};
    ctx.eventsIn = uw::EventBlock{synthEvents.data(), static_cast<std::uint32_t>(n)};
    synth->process(ctx);

    for (std::uint32_t c = 0; c < b.outChannels; ++c) {
        if (synthChannels == 0) {
            std::fill_n(b.out[c], b.frames, 0.0f);
            continue;
        }
        const float* from = scratchPointers[std::min(c, synthChannels - 1)];   // a mono synth goes to both sides
        std::copy_n(from, b.frames, b.out[c]);
    }
}

void SpikeEngine::stopProcessing() noexcept {
    if (synthActive) synth->stopProcessing();
}

void SpikeEngine::reset() noexcept {
    sounding.reset();
    wasPlaying = false;
    if (synthActive) synth->reset();
}

} // namespace umpire::plugin
