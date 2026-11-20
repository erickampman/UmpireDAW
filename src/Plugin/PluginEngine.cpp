#include "Plugin/PluginEngine.h"

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

// Addressed to one key: what goes with a note (and is cut with it).
bool isPerNote(const uw::MidiMessage& m) {
    if (!ump::isChannelVoice(m)) return false;
    const ump::Status s = ump::status(m);
    if (s == ump::Status::PolyPressure) return true;
    return isMidi2(m) && (s == ump::Status::RegisteredPerNoteController || s == ump::Status::AssignablePerNoteController ||
                          s == ump::Status::PerNotePitchBend || s == ump::Status::PerNoteManagement);
}

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

PluginEngine::PluginEngine() {
    for (TakeBuffer& t : takes) t.events.resize(kMaxTakeEvents);
    chaseIndex.assign(kKeys, -1);
    outEvents.resize(kMaxEvents);
    synthEvents.resize(kMaxEvents);
    liveBlock.resize(kMaxEvents);
}

PluginEngine::~PluginEngine() = default;   // `owned` frees every PlayData, the one playing included

// --- main thread -----------------------------------------------------------------------

std::unique_ptr<PluginEngine::SynthSlot> PluginEngine::makeSlot(uw::HostedPlugin* plugin) {
    auto slot    = std::make_unique<SynthSlot>();
    slot->plugin = plugin;
    if (plugin != nullptr && plugin->activate(sampleRate, maxFrames)) {
        slot->active                = true;
        const uw::PortLayout layout = plugin->portLayout();
        slot->notePort              = layout.noteInputs.empty() ? -1 : 0;
        slot->midi2 = !layout.noteInputs.empty() && (layout.noteInputs[0].supportedDialects & uw::NoteDialect::Midi2) != 0;
        const int main = uw::mainAudioOutput(layout);
        slot->channels = main >= 0 ? layout.audioOutputs[static_cast<std::size_t>(main)].channelCount : 0;
    }
    slot->scratch.assign(slot->channels, std::vector<float>(maxFrames, 0.0f));
    slot->pointers.resize(slot->channels);
    for (std::uint32_t c = 0; c < slot->channels; ++c) slot->pointers[c] = slot->scratch[c].data();
    return slot;
}

bool PluginEngine::activate(double rate, std::uint32_t frames) {
    sampleRate = rate > 0 ? rate : 48000.0;
    maxFrames  = frames;
    auto slot  = makeSlot(synth);
    const bool ok = synth == nullptr || slot->active;
    running = latest = slot.get();
    slots.push_back(std::move(slot));
    wasPlaying = wasRolling = false;
    sounding.reset();
    isActive = true;
    return ok;
}

// Deactivates a slot's synth and frees the slot; a synth that isn't the
// latest given is the caller's to destroy (retiredSynth()).
void PluginEngine::retireSlot(SynthSlot* slot) {
    if (slot == nullptr) return;
    if (slot->active) slot->plugin->deactivate();
    if (slot->plugin != nullptr && slot->plugin != synth) retired.push_back(slot->plugin);
    if (latest == slot) latest = nullptr;
    std::erase_if(slots, [slot](const auto& s) { return s.get() == slot; });
}

void PluginEngine::deactivate() {
    if (!isActive) return;
    // The audio thread isn't running now (CLAP), so a take in progress can be
    // ended here: Bitwig deactivates around a change of render mode, say.
    if (recordingSlot >= 0) endTake(expectedTick);
    takeBlocked = false;
    // Swaps the audio thread never took: the newest is the synth now.
    SynthSlot* slot = nullptr;
    while (slotIn.pop(slot)) {
        retireSlot(running);
        running = slot;
    }
    while (slotOut.pop(slot)) retireSlot(slot);
    slotsInFlight = 0;
    retireSlot(running);
    running = latest = nullptr;
    isActive = false;
}

PluginEngine::Swap PluginEngine::swapSynth(uw::HostedPlugin* next) {
    if (!isActive) {
        synth = next;
        return Swap::Done;
    }
    retiredSynth();   // what's come back makes room
    if (slotsInFlight >= 4) return Swap::Busy;
    uw::HostedPlugin* const was = synth;
    synth                       = next;
    auto slot                   = makeSlot(next);
    if (!slotIn.push(slot.get())) {   // can't happen below the limit; undone if it does
        synth = was;
        if (slot->active) next->deactivate();
        return Swap::Busy;
    }
    ++slotsInFlight;
    latest = slot.get();
    slots.push_back(std::move(slot));
    return Swap::Pending;
}

uw::HostedPlugin* PluginEngine::retiredSynth() {
    SynthSlot* back = nullptr;
    while (slotOut.pop(back)) {
        if (slotsInFlight > 0) --slotsInFlight;
        retireSlot(back);
    }
    if (retired.empty()) return nullptr;
    uw::HostedPlugin* const plugin = retired.back();
    retired.pop_back();
    return plugin;
}

// --- the lane side ---------------------------------------------------------------------

void PluginEngine::setEvents(const std::vector<ClipEvent>& songEvents) {
    auto data = std::make_unique<PlayData>();
    std::set<int> tuned;
    for (const ClipEvent& e : songEvents) {
        if (!ump::isChannelVoice(e.message)) continue;
        data->events.push_back(e);
        if (isPitch725(e.message)) tuned.insert(noteKey(e.message));
        if (isPitch725(e.message) || isReset(e.message)) data->tunings.push_back(e);
    }
    const auto byTick = [](const ClipEvent& a, const ClipEvent& b) { return a.tick < b.tick; };
    std::stable_sort(data->events.begin(), data->events.end(), byTick);
    std::stable_sort(data->tunings.begin(), data->tunings.end(), byTick);
    data->tunedKeys.assign(tuned.begin(), tuned.end());

    unsent = std::move(data);   // replaces one not sent yet: only the latest matters
    collectGarbage();
}

bool PluginEngine::collectTake(FinishedTake& out) {
    TakeBuffer* oldest = nullptr;
    for (TakeBuffer& t : takes)
        if (t.state.load(std::memory_order_acquire) == kTakeReady && (oldest == nullptr || t.serial < oldest->serial))
            oldest = &t;
    if (oldest == nullptr) return false;
    out = FinishedTake{};
    out.clip.events.assign(oldest->events.begin(), std::next(oldest->events.begin(), static_cast<std::ptrdiff_t>(oldest->count)));
    std::stable_sort(out.clip.events.begin(), out.clip.events.end(),
                     [](const ClipEvent& a, const ClipEvent& b) { return a.tick < b.tick; });
    // Recorded on play (Logic, GarageBand: they never say when they record,
    // nor when they count in), nothing before where recording started
    // (recordStartOnPlay: after a count-in, before bar 1) is kept: not its
    // notes, nor the rest of a note begun before it. Then the take starts on
    // the beat of its first note, not where the host began playing; what
    // came before that beat (controllers, pitch) moves to it. Decided with
    // the author, 2026-11-20 (plugin-version.md, "AUv2 for Logic").
    Tick       start = oldest->start;
    const Tick from  = std::clamp(oldest->recordFrom, oldest->start, oldest->end);
    if (from > start) {
        std::vector<bool> cut(kKeys, false);   // keys whose note-on was before `from`
        std::vector<ClipEvent> kept;
        kept.reserve(out.clip.events.size());
        for (const ClipEvent& e : out.clip.events) {
            const uw::MidiMessage& m       = e.message;
            const bool             perNote = ump::isNoteOn(m) || ump::isNoteOff(m) || isPerNote(m);
            const auto             key     = perNote ? static_cast<std::size_t>(noteKey(m)) : 0;
            if (e.tick < from) {
                if (ump::isNoteOn(m)) cut[key] = true;
                continue;
            }
            if (ump::isNoteOn(m)) cut[key] = false;
            else if (perNote && cut[key]) {
                if (ump::isNoteOff(m)) cut[key] = false;
                continue;
            }
            kept.push_back(e);
        }
        out.clip.events = std::move(kept);
        start           = from;
    }
    if (oldest->fromFirstNote) {
        const auto on = std::find_if(out.clip.events.begin(), out.clip.events.end(),
                                     [](const ClipEvent& e) { return ump::isNoteOn(e.message); });
        if (on != out.clip.events.end()) {
            const Tick q    = kTicksPerQuarter;
            const Tick beat = on->tick >= 0 ? on->tick / q * q : -((-on->tick + q - 1) / q) * q;
            start           = std::clamp(beat, start, oldest->end);
        }
    }
    out.start       = start;
    out.clip.length = oldest->end - start;
    for (ClipEvent& e : out.clip.events) e.tick = std::max<Tick>(e.tick - start, 0);
    oldest->state.store(kTakeIdle, std::memory_order_release);
    return true;
}

void PluginEngine::collectGarbage() {
    PlayData* done = nullptr;
    while (toMain.pop(done))
        owned.erase(std::remove_if(owned.begin(), owned.end(), [&](const auto& p) { return p.get() == done; }),
                    owned.end());
    if (unsent && toAudio.push(unsent.get())) owned.push_back(std::move(unsent));
}

LiveStats PluginEngine::takeStats() {
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

void PluginEngine::onMidi(uw::MidiSourceHandle, const uw::TimedMidiMessage& msg) noexcept {
    if (!live.push(msg)) statDropped.fetch_add(1, std::memory_order_relaxed);
}

// --- audio thread ----------------------------------------------------------------------

void PluginEngine::emit(std::uint32_t offset, const uw::MidiMessage& m) noexcept {
    if (outCount >= outEvents.size()) {
        statDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    outEvents[outCount++] = Out{offset, m};
}

void PluginEngine::endPlayNotes(std::uint32_t offset) noexcept {
    if (sounding.none()) return;
    for (int key = 0; key < kKeys; ++key)
        if (sounding.test(static_cast<std::size_t>(key))) emit(offset, noteOff(key));
    sounding.reset();
}

// Each key the lane tunes gets the Pitch 7.25 in force at `at`, or a reset
// where none is (App/Player.cpp's chase).
void PluginEngine::chase(Tick at, std::uint32_t offset) noexcept {
    if (playing == nullptr || playing->tunedKeys.empty()) return;
    const auto& tunings = playing->tunings;
    for (std::size_t i = 0; i < tunings.size() && tunings[i].tick < at; ++i)
        chaseIndex[static_cast<std::size_t>(noteKey(tunings[i].message))] = static_cast<int>(i);
    for (const int key : playing->tunedKeys) {
        const int i = chaseIndex[static_cast<std::size_t>(key)];
        emit(offset, i >= 0 ? tunings[static_cast<std::size_t>(i)].message : resetKey(key));
    }
    for (std::size_t i = 0; i < tunings.size() && tunings[i].tick < at; ++i)
        chaseIndex[static_cast<std::size_t>(noteKey(tunings[i].message))] = -1;
}

void PluginEngine::playRange(Tick from, Tick to, const Piece& p) noexcept {
    if (playing == nullptr || to <= from) return;
    const auto& events = playing->events;
    auto it = std::lower_bound(events.begin(), events.end(), from,
                               [](const ClipEvent& e, Tick t) { return e.tick < t; });
    const double first = p.first, last = p.first + p.frames - 1;
    for (; it != events.end() && it->tick < to; ++it) {
        double frame = first + std::round(static_cast<double>(it->tick - p.tick) / p.ticksPerFrame);
        frame        = std::clamp(frame, first, last);
        const uw::MidiMessage& m = it->message;
        if (ump::isNoteOn(m)) sounding.set(static_cast<std::size_t>(noteKey(m)));
        else if (ump::isNoteOff(m)) sounding.reset(static_cast<std::size_t>(noteKey(m)));
        emit(static_cast<std::uint32_t>(frame), m);
    }
}

Tick recordStartOnPlay(Tick playFrom, bool stoppedSeen, Tick stoppedAt, double quartersPerBar) {
    Tick from = playFrom;
    if (stoppedSeen && stoppedAt > playFrom) {
        const Tick   q     = kTicksPerQuarter;
        const Tick   gap   = stoppedAt - playFrom;
        const double bars  = static_cast<double>(gap) / static_cast<double>(q) / std::max(quartersPerBar, 0.25);
        const Tick   beats = (gap + q / 2) / q;
        if (bars <= 6.0 + 1e-6 && std::llabs(gap - beats * q) <= q / 64) from = stoppedAt;
    }
    return std::max<Tick>(from, 0);
}

TakeStart PluginEngine::lastTakeStart() const {
    TakeStart s;
    s.serial      = startSerial.load(std::memory_order_acquire);
    s.playFrom    = startPlayFrom.load(std::memory_order_relaxed);
    s.stoppedAt   = startStoppedAt.load(std::memory_order_relaxed);
    s.recordFrom  = startRecordFrom.load(std::memory_order_relaxed);
    s.stoppedSeen = startStoppedSeen.load(std::memory_order_relaxed);
    return s;
}

void PluginEngine::startTake(Tick at, bool fromFirstNote, Tick recordFrom) noexcept {
    for (int i = 0; i < 2; ++i) {
        TakeBuffer& t = takes[i];
        if (t.state.load(std::memory_order_acquire) != kTakeIdle) continue;
        t.count = 0;
        t.start = at;
        t.end   = at;
        t.fromFirstNote = fromFirstNote;
        t.recordFrom    = recordFrom;
        liveHeld.reset();
        t.state.store(kTakeRecording, std::memory_order_relaxed);
        recordingSlot = i;
        recordingNow.store(true, std::memory_order_relaxed);
        return;
    }
    takeBlocked = true;   // both wait to be collected: this pass isn't recorded
    missedTakes.fetch_add(1, std::memory_order_relaxed);
}

void PluginEngine::record(Tick tick, const uw::MidiMessage& m) noexcept {
    if (recordingSlot < 0 || !ump::isChannelVoice(m)) return;
    TakeBuffer& t = takes[recordingSlot];
    if (t.count >= t.events.size()) {
        statDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    t.events[t.count++] = ClipEvent{tick, m};
    if (ump::isNoteOn(m)) liveHeld.set(static_cast<std::size_t>(noteKey(m)));
    else if (ump::isNoteOff(m)) liveHeld.reset(static_cast<std::size_t>(noteKey(m)));
}

// Notes still held end with the take, so it never leaves one sounding.
void PluginEngine::endTake(Tick at) noexcept {
    if (recordingSlot < 0) return;
    TakeBuffer& t = takes[recordingSlot];
    at            = std::max(at, t.start);
    if (liveHeld.any())
        for (int key = 0; key < kKeys; ++key)
            if (liveHeld.test(static_cast<std::size_t>(key))) record(at, noteOff(key));
    liveHeld.reset();
    t.end    = at;
    t.serial = nextSerial++;
    t.state.store(kTakeReady, std::memory_order_release);
    recordingSlot = -1;
    recordingNow.store(false, std::memory_order_relaxed);
}

// Live input since the last block, each message at a sample offset from its
// capture time: a block's frames are taken to be the block's length of time
// before process() was called, so the delay is about a block, steady, and
// the jitter is the host's callback jitter, not a block's. Played through
// now; recorded piece by piece (recordLive()).
void PluginEngine::gatherLive(const Block& b) noexcept {
    liveCount = liveNext = 0;
    uw::TimedMidiMessage timed;
    if (offline()) {   // the render extension: a render never hears what's played now
        while (live.pop(timed)) {}
        return;
    }
    const double          framesPerNs = sampleRate / 1e9;
    const uw::Nanoseconds blockNs     = static_cast<uw::Nanoseconds>(static_cast<double>(b.frames) / framesPerNs);
    const uw::Nanoseconds windowStart = b.now - blockNs;
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
        // (App/TrackController.cpp), so the lane is MIDI 2.0.
        uw::MidiMessage converted[ump::kMaxTranslatedMessages];
        std::size_t     count = 1;
        converted[0]          = timed.message;
        if (ump::messageType(timed.message) == ump::MessageType::Midi1ChannelVoice)
            count = ump::upconvert(timed.message, upconverter, converted, std::size(converted));
        for (std::size_t i = 0; i < count; ++i) {
            emit(offset, converted[i]);
            if (liveCount < liveBlock.size()) liveBlock[liveCount++] = Out{offset, converted[i]};
            else statDropped.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void PluginEngine::recordLive(const Piece& p) noexcept {
    const std::uint32_t end = p.first + p.frames;
    for (; liveNext < liveCount && liveBlock[liveNext].offset < end; ++liveNext) {
        const Out& e = liveBlock[liveNext];
        if (p.recording)
            record(p.tick + std::llround(static_cast<double>(e.offset - p.first) * p.ticksPerFrame), e.message);
    }
}

// A synth swapped in (swapSynth()): the old one stops here, on the audio
// thread as CLAP says, and goes back to be deactivated. The lane starts again
// on the new one from where it is, its tuning chased.
void PluginEngine::adoptSynth() noexcept {
    SynthSlot* next = nullptr;
    while (slotIn.pop(next)) {
        if (running != nullptr) {
            if (running->active) running->plugin->stopProcessing();
            slotOut.push(running);   // room: no more than four are in flight
        }
        running     = next;
        dataChanged = true;
    }
}

void PluginEngine::adoptPlayData() noexcept {
    PlayData* next = nullptr;
    while (toAudio.pop(next)) {
        if (playing != nullptr) toMain.push(playing);
        playing     = next;
        dataChanged = true;   // the old lane's notes end; the new one's tuning is chased
    }
}

void PluginEngine::process(const Block& b) noexcept {
    if (!isActive || b.frames == 0 || b.frames > maxFrames) {
        for (std::uint32_t c = 0; c < b.outChannels; ++c) std::fill_n(b.out[c], b.frames, 0.0f);
        return;
    }
    outCount = 0;
    adoptSynth();
    adoptPlayData();
    gatherLive(b);

    // The block's pieces, split where a change (a transport event, a tempo
    // change; the arm) changes them. A change at a piece's own first frame
    // replaces it. Past kMaxPieces, later changes are ignored.
    Piece       pieces[kMaxPieces];
    std::size_t count = 1;
    pieces[0].transport = b.transport;
    pieces[0].armed     = b.armed;
    for (std::uint32_t i = 0; i < b.changeCount; ++i) {
        const TransportChange& c = b.changes[i];
        if (c.frame >= b.frames || c.frame < pieces[count - 1].first) continue;
        if (c.frame > pieces[count - 1].first) {
            if (count == kMaxPieces) break;
            Piece& next    = pieces[count];
            next.first     = c.frame;
            next.transport = pieces[count - 1].transport;
            next.continues = true;
            ++count;
        }
        Piece& piece = pieces[count - 1];
        if (c.hasTransport) {
            piece.transport = c.transport;
            piece.continues = false;
        }
        piece.armed = c.armed;
    }
    for (std::size_t i = 0; i < count; ++i) {
        pieces[i].frames = (i + 1 < count ? pieces[i + 1].first : b.frames) - pieces[i].first;
        processPiece(pieces[i]);
    }
    const HostTransport& last = pieces[count - 1].transport;
    seenTempo.store(last.tempo > 0 ? last.tempo : 120.0, std::memory_order_relaxed);
    seenBeatsPerBar.store(last.tsigDen == 4 && last.tsigNum > 0 ? last.tsigNum : 4, std::memory_order_relaxed);
    // For the editor's playhead: the position at the block's end while
    // rolling, else where the host says it is.
    const Piece& lastPiece = pieces[count - 1];
    const bool   rolling   = last.playing && last.hasBeats;
    if (rolling)
        seenPosition.store(lastPiece.tick + std::llround(static_cast<double>(lastPiece.frames) * lastPiece.ticksPerFrame),
                           std::memory_order_relaxed);
    else if (last.hasBeats)
        seenPosition.store(std::llround(last.songBeats * static_cast<double>(kTicksPerQuarter)), std::memory_order_relaxed);
    seenPlaying.store(rolling, std::memory_order_relaxed);

    runSynth(b);
    steadyFrames += b.frames;
}

void PluginEngine::processPiece(Piece& p) noexcept {
    const HostTransport& t     = p.transport;
    const double         tempo = t.tempo > 0 ? t.tempo : 120.0;
    p.ticksPerFrame            = tempo / 60.0 * static_cast<double>(kTicksPerQuarter) / sampleRate;
    const bool rolling         = t.playing && t.hasBeats;
    p.tick = rolling && !p.continues ? std::llround(t.songBeats * static_cast<double>(kTicksPerQuarter)) : expectedTick;
    const Tick ticks           = std::llround(static_cast<double>(p.frames) * p.ticksPerFrame);
    const Tick end             = p.tick + ticks;
    const RecordMode mode      = recordMode();
    const bool onPlay          = mode == RecordMode::Play;
    const bool rec = rolling && mode != RecordMode::Off && (t.recording || onPlay) && p.armed && !offline();
    if (!rolling && t.hasBeats) {   // where it stands: a count-in ends here
        stoppedAt   = std::llround(t.songBeats * static_cast<double>(kTicksPerQuarter));
        stoppedSeen = true;
    }

    // A jump: a locate, a loop wrap. Tempo changes move the position a
    // little against the last piece's guess, so only more than half a piece
    // counts.
    const Tick tolerance = ticks / 2 + 16;
    const bool jumped    = wasRolling && rolling && std::llabs(p.tick - expectedTick) > tolerance;

    // Recording follows the host's record button and the arm (disarming ends
    // the take, arming while the host records punches in); a jump ends the
    // take where the last piece ended and starts another here.
    if (recordingSlot >= 0 && (!rec || jumped)) endTake(jumped ? expectedTick : p.tick);
    if (!rec) takeBlocked = false;
    if (rec && recordingSlot < 0 && (!takeBlocked || jumped)) {
        takeBlocked = false;
        Tick from   = p.tick;
        if (onPlay) {
            // A fresh start may follow a count-in; a jump (a cycle) doesn't.
            const double qpb = t.tsigDen > 0 && t.tsigNum > 0 ? t.tsigNum * 4.0 / t.tsigDen : 4.0;
            from             = wasRolling ? std::max<Tick>(p.tick, 0) : recordStartOnPlay(p.tick, stoppedSeen, stoppedAt, qpb);
            startPlayFrom.store(p.tick, std::memory_order_relaxed);
            startStoppedAt.store(stoppedAt, std::memory_order_relaxed);
            startRecordFrom.store(from, std::memory_order_relaxed);
            startStoppedSeen.store(stoppedSeen && !wasRolling, std::memory_order_relaxed);
            startSerial.fetch_add(1, std::memory_order_release);
        }
        startTake(p.tick, onPlay, from);
    }
    p.recording = recordingSlot >= 0;

    // Playback from the host's position; while recording, the lane is silent
    // (only while armed: an instance that isn't plays on), except on play,
    // where every pass with the arm on records and the lane would never sound.
    const bool play = rolling && (!p.recording || onPlay) && playing != nullptr;
    if (wasPlaying && !play) endPlayNotes(p.first);
    if (play) {
        const bool fresh = !wasPlaying || dataChanged || jumped;
        if (fresh) {
            endPlayNotes(p.first);
            chase(p.tick, p.first);
        }
        playRange(fresh ? p.tick : expectedTick, end, p);
    } else if (dataChanged) {
        endPlayNotes(p.first);
    }
    dataChanged  = false;
    wasPlaying   = play;
    wasRolling   = rolling;
    expectedTick = rolling ? end : p.tick;

    recordLive(p);
}

void PluginEngine::runSynth(const Block& b) noexcept {
    // Stable insertion sort by offset: nearly sorted already, and std::stable_sort may allocate.
    for (std::size_t i = 1; i < outCount; ++i) {
        const Out   e = outEvents[i];
        std::size_t j = i;
        for (; j > 0 && outEvents[j - 1].offset > e.offset; --j) outEvents[j] = outEvents[j - 1];
        outEvents[j] = e;
    }

    SynthSlot* const slot = running;
    if (slot == nullptr || !slot->active) {
        for (std::uint32_t c = 0; c < b.outChannels; ++c) std::fill_n(b.out[c], b.frames, 0.0f);
        return;
    }

    std::size_t n = 0;
    if (slot->notePort >= 0) {
        uw::MidiMessage converted[ump::kMaxTranslatedMessages];
        for (std::size_t i = 0; i < outCount; ++i) {
            const uw::MidiMessage& m = outEvents[i].message;
            std::size_t            count = 1;
            converted[0]                 = m;
            if (!slot->midi2 && isMidi2(m)) count = ump::downconvert(m, converted, std::size(converted));
            for (std::size_t k = 0; k < count && n < synthEvents.size(); ++k) {
                uw::Event& e   = synthEvents[n++];
                e              = uw::Event{};
                e.sampleOffset = outEvents[i].offset;
                e.type         = uw::EventType::Midi;
                e.midi         = converted[k];
                e.portIndex    = static_cast<std::uint16_t>(slot->notePort);
            }
        }
    }

    const std::uint32_t channels = slot->channels;
    for (std::uint32_t c = 0; c < channels; ++c) std::fill_n(slot->pointers[c], b.frames, 0.0f);

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
    ctx.audioOut = uw::AudioBuffer{slot->pointers.data(), channels, b.frames};
    ctx.eventsIn = uw::EventBlock{synthEvents.data(), static_cast<std::uint32_t>(n)};
    slot->plugin->process(ctx);

    for (std::uint32_t c = 0; c < b.outChannels; ++c) {
        if (channels == 0) {
            std::fill_n(b.out[c], b.frames, 0.0f);
            continue;
        }
        const float* from = slot->pointers[std::min(c, channels - 1)];   // a mono synth goes to both sides
        std::copy_n(from, b.frames, b.out[c]);
    }
}

void PluginEngine::stopProcessing() noexcept {
    if (running != nullptr && running->active) running->plugin->stopProcessing();
}

void PluginEngine::reset() noexcept {
    sounding.reset();
    wasPlaying = false;
    if (running != nullptr && running->active) running->plugin->reset();
}

} // namespace umpire::plugin
