// Milestones P1 and P2 (docs/plugin-version.md): the engine inside the
// plugin (src/Plugin/PluginEngine.h), driven as a host would drive it,
// with a fake synth that only keeps the events it's given. Live input at
// its sample offsets, MIDI 1.0 upconverted, a MIDI 1.0 synth downconverted,
// offline render ignoring live input; recording from the host's position;
// playback from it, with the chase on a jump and notes ended on stop; and
// the setup file and the saved state (src/Plugin/PluginSetup.h). P2: takes
// from a jump while recording, two take buffers, a tempo change inside a
// block, the lane (src/Plugin/Lane.h: the newest take plays where takes
// overlap) and the lane thread (src/Plugin/LaneKeeper.h), state version 2.
// P3: the editor's side of the lane (testEditor): a take's notes edited
// through LaneKeeper and heard, a note moved before the take, delete and
// bring to front, undo and redo of each and of a recorded take, the covered
// spans the lane draws, the grid in song time (GridModel's origin), and the
// host's position for the playhead.
// Silent: no MIDI hardware, no audio, no window.

#include <chrono>
#include <cmath>
#include <thread>
#include <cstdio>
#include <string>
#include <vector>

#include "App/ClipFile.h"
#include "App/GridModel.h"
#include "Plugin/Lane.h"
#include "Plugin/LaneKeeper.h"
#include "Plugin/PluginEngine.h"
#include "Plugin/PluginSetup.h"
#include "uw/ump/Ump.h"

namespace {

using namespace umpire;
using namespace umpire::plugin;
namespace ump = uw::ump;

int failures = 0;

void check(const char* what, bool ok, const std::string& detail = {}) {
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str());
    if (!ok) ++failures;
}

// A synth that keeps what it's given, block by block, and makes no sound.
class FakeSynth final : public uw::HostedPlugin {
public:
    explicit FakeSynth(uw::NoteDialectMask dialects = uw::NoteDialect::Midi2 | uw::NoteDialect::Midi)
        : dialects(dialects) {
        desc.id = "test.fake-synth";
    }

    struct Got {
        std::uint32_t   offset;
        uw::MidiMessage message;
    };
    std::vector<std::vector<Got>> blocks;
    bool                          active = false;

    std::vector<Got> last() const { return blocks.empty() ? std::vector<Got>{} : blocks.back(); }

    const uw::PluginDescriptor& descriptor() const override { return desc; }
    uw::PortLayout              portLayout() const override {
        uw::PortLayout l;
        l.audioOutputs.push_back({"Out", 2, true});
        l.noteInputs.push_back({"Notes", dialects, (dialects & uw::NoteDialect::Midi2) ? uw::NoteDialect::Midi2 : uw::NoteDialect::Midi});
        return l;
    }
    std::vector<uw::ParameterInfo> parameters() const override { return {}; }
    bool activate(double, std::uint32_t) override { return active = true; }
    void deactivate() override { active = false; }
    void process(const uw::ProcessContext& ctx) noexcept override {
        std::vector<Got> got;
        for (std::uint32_t i = 0; i < ctx.eventsIn.count; ++i)
            got.push_back({ctx.eventsIn.events[i].sampleOffset, ctx.eventsIn.events[i].midi});
        blocks.push_back(got);
        for (std::uint32_t c = 0; c < ctx.audioOut.channelCount; ++c)
            for (std::uint32_t f = 0; f < ctx.frameCount; ++f) ctx.audioOut.channels[c][f] = 0.0f;
    }
    void   stopProcessing() noexcept override {}
    double paramValue(std::uint32_t) const override { return 0; }
    void   flushParams(const uw::EventBlock&, uw::EventSink*) override {}
    bool   saveState(std::vector<std::byte>&) const override { return true; }
    bool   loadState(const std::byte*, std::size_t) override { return true; }
    void   idle() override {}

private:
    uw::NoteDialectMask  dialects;
    uw::PluginDescriptor desc;
};

constexpr double          kRate   = 48000;
constexpr std::uint32_t   kFrames = 480;   // 10 ms; at 120 bpm, 0.02 beats
constexpr uw::Nanoseconds kNow    = 1'000'000'000'000;
constexpr double          kBeatsPerBlock = 0.02;

// A host: blocks of kFrames, its transport and clock moving on together.
struct Host {
    explicit Host(PluginEngine& e) : engine(e) {}
    PluginEngine&       engine;
    std::vector<float> left = std::vector<float>(kFrames), right = std::vector<float>(kFrames);
    float*             out[2] = {left.data(), right.data()};
    uw::Nanoseconds    now    = kNow;
    HostTransport      transport;
    bool               armed = true;   // the Record parameter

    void block() {
        Block b;
        b.frames      = kFrames;
        b.transport   = transport;
        b.armed       = armed;
        b.now         = now;
        b.out         = out;
        b.outChannels = 2;
        engine.process(b);
        now += 10'000'000;
        if (transport.playing) transport.songBeats += kBeatsPerBlock;
    }
    void play(double fromBeats, bool recording = false) {
        transport.playing   = true;
        transport.hasBeats  = true;
        transport.recording = recording;
        transport.songBeats = fromBeats;
    }
};

uw::TimedMidiMessage at(uw::Nanoseconds when, const uw::MidiMessage& m) { return {when, m}; }

bool isPitch725(const uw::MidiMessage& m) {
    return ump::messageType(m) == ump::MessageType::Midi2ChannelVoice &&
           ump::status(m) == ump::Status::RegisteredPerNoteController && (m.words[0] & 0xFF) == 3;
}

uw::MidiMessage pitch725(std::uint8_t key, std::uint32_t value) {
    uw::MidiMessage m;
    m.words[0]  = 0x40000000u | 0x0u << 20 | std::uint32_t(key) << 8 | 3;   // registered per-note controller 3
    m.words[1]  = value;
    m.wordCount = 2;
    return m;
}

void testLiveInput() {
    FakeSynth   synth;
    PluginEngine engine;
    engine.setSynth(&synth);
    check("activates with its synth", engine.activate(kRate, 4096) && synth.active);
    Host host(engine);

    // 5 ms before the block's call: half a block in, at 240 of 480 frames.
    engine.onMidi(1, at(host.now - 5'000'000, ump::midi2NoteOn(0, 0, 60, 0x8000)));
    host.block();
    auto got = synth.last();
    check("live note reaches the synth", got.size() == 1 && ump::isNoteOn(got[0].message));
    check("at its offset from its capture time", got.size() == 1 && got[0].offset == 240,
          got.empty() ? "" : std::to_string(got[0].offset));

    // Older than a block: frame 0. Newer than the call: the next block.
    engine.onMidi(1, at(host.now - 50'000'000, ump::midi2NoteOff(0, 0, 60)));
    engine.onMidi(1, at(host.now + 1'000'000, ump::midi2NoteOn(0, 0, 62, 0x8000)));
    host.block();
    got = synth.last();
    check("a late message plays at frame 0, a future one waits", got.size() == 1 && got[0].offset == 0 && ump::isNoteOff(got[0].message));
    host.block();
    check("the future one plays in the next block", synth.last().size() == 1);

    // MIDI 1.0 input is upconverted.
    uw::MidiMessage m1;
    m1.words[0]  = 0x20903C64;   // MT 2, note on, key 60, velocity 100
    m1.wordCount = 1;
    engine.onMidi(1, at(host.now - 1'000'000, m1));
    host.block();
    got = synth.last();
    check("MIDI 1.0 input arrives as MIDI 2.0",
          got.size() == 1 && ump::messageType(got[0].message) == ump::MessageType::Midi2ChannelVoice);

    const LiveStats stats = engine.takeStats();
    check("timing is counted", stats.count == 4 && stats.clamped == 1, std::to_string(stats.count));

    engine.setOffline(true);
    engine.onMidi(1, at(host.now - 1'000'000, ump::midi2NoteOn(0, 0, 64, 0x8000)));
    host.block();
    check("offline render ignores live input", synth.last().empty());
    engine.setOffline(false);
    host.block();
    check("and drops it, rather than playing it late", synth.last().empty());
    engine.deactivate();
    check("deactivates its synth", !synth.active);
}

void testMidi1Synth() {
    FakeSynth   synth(uw::NoteDialect::Midi);
    PluginEngine engine;
    engine.setSynth(&synth);
    engine.activate(kRate, 4096);
    Host host(engine);
    engine.onMidi(1, at(host.now - 1'000'000, ump::midi2NoteOn(0, 0, 60, 0x8000)));
    host.block();
    const auto got = synth.last();
    check("a MIDI 1.0 synth gets MIDI 1.0",
          got.size() == 1 && ump::messageType(got[0].message) == ump::MessageType::Midi1ChannelVoice);
}

void testRecording() {
    FakeSynth   synth;
    PluginEngine engine;
    engine.setSynth(&synth);
    engine.activate(kRate, 4096);
    Host host(engine);

    FinishedTake none;
    check("nothing to collect before a take", !engine.collectTake(none));

    host.play(4.0, true);
    engine.onMidi(1, at(host.now - 5'000'000, ump::midi2NoteOn(0, 0, 60, 0x8000)));
    host.block();   // beat 4.00
    check("records while the host records", engine.recording());
    host.block();   // 4.02
    engine.onMidi(1, at(host.now - 5'000'000, ump::midi2NoteOff(0, 0, 60)));
    host.block();   // 4.04
    engine.onMidi(1, at(host.now - 5'000'000, ump::midi2NoteOn(0, 0, 64, 0x8000)));   // held to the end
    host.block();   // 4.06
    check("plays through while recording", synth.last().size() == 1);
    host.transport.recording = false;   // the host stops recording, still playing
    const double stopBeats   = host.transport.songBeats;
    host.block();   // 4.08
    check("stops when the host stops", !engine.recording());

    FinishedTake finished;
    check("the take is collected", engine.collectTake(finished));
    const Clip& take = finished.clip;
    check("it starts where recording began", finished.start == 4 * kTicksPerQuarter, std::to_string(finished.start));
    // 240 frames at 120 bpm and 48 kHz: 0.01 beats, 403 ticks.
    const Tick first = 403;
    check("four events: two notes, the held one ended", take.events.size() == 4, std::to_string(take.events.size()));
    if (take.events.size() == 4) {
        check("its ticks are from the take's start", take.events[0].tick == first, std::to_string(take.events[0].tick));
        check("the off a block and a bit later", take.events[1].tick == first + 2 * 806 + 1, std::to_string(take.events[1].tick));
        const Tick end = std::llround(stopBeats * kTicksPerQuarter) - finished.start;
        check("a held note ends with the take", ump::isNoteOff(take.events[3].message) && take.events[3].tick == end);
        check("the clip runs to the take's end", take.length == end);
    }
    check("collected once", !engine.collectTake(finished));

    // Through a MIDI Clip File and back, exactly (the state's takes).
    const auto bytes = clipfile::write(take.events, take.length, {120, 4});
    const auto read  = clipfile::read(bytes.data(), bytes.size());
    check("the take survives a MIDI Clip File", read.ok && read.events.size() == take.events.size() &&
                                                    read.length == take.length && read.events[0].tick == take.events[0].tick);

    // Offline, nothing records.
    engine.setOffline(true);
    host.play(8.0, true);
    host.block();
    check("an offline render doesn't record", !engine.recording());
}

void testPlayback() {
    FakeSynth   synth;
    PluginEngine engine;
    engine.setSynth(&synth);
    engine.activate(kRate, 4096);
    Host host(engine);

    Clip clip;
    clip.length = 2 * kTicksPerQuarter;
    clip.events.push_back({0, pitch725(60, 0x7A000000)});
    clip.events.push_back({std::llround(1.01 * kTicksPerQuarter), ump::midi2NoteOn(0, 0, 60, 0x8000)});
    clip.events.push_back({std::llround(1.50 * kTicksPerQuarter), ump::midi2NoteOff(0, 0, 60)});
    engine.setEvents(clip.events);

    host.play(0.0);
    host.block();
    auto got = synth.last();
    check("play from the start: the key reset, then its Pitch 7.25", got.size() == 2 &&
                                                                    !isPitch725(got[0].message) && isPitch725(got[1].message));

    while (host.transport.songBeats < 1.0 - 1e-9) host.block();
    host.block();   // beat 1.00 to 1.02
    got = synth.last();
    check("the note plays at its offset", got.size() == 1 && ump::isNoteOn(got[0].message) && got[0].offset == 240,
          got.empty() ? "" : std::to_string(got[0].offset));

    // A locate into the note: it ends, and the tuning is chased.
    host.transport.songBeats = 1.2;
    host.block();
    got = synth.last();
    check("a jump ends the note sounding and chases Pitch 7.25", got.size() == 2 && ump::isNoteOff(got[0].message) &&
                                                                  isPitch725(got[1].message) && got[0].offset == 0);

    // Back before the note, then play into it and stop.
    host.transport.songBeats = 1.0;
    host.block();
    host.block();
    host.transport.playing = false;
    host.block();
    got = synth.last();
    check("stopping ends the notes sounding", got.size() == 1 && ump::isNoteOff(got[0].message));
    host.block();
    check("and then nothing", synth.last().empty());

    // While recording, the clip doesn't play.
    host.play(1.0, true);
    host.block();
    host.block();
    check("the clip is silent while recording", synth.last().empty());
    host.transport.recording = false;
    host.block();
    FinishedTake take;
    engine.collectTake(take);

    // A new lane while playing: the old one's notes end.
    host.play(1.0);
    host.block();
    host.block();
    engine.setEvents({});
    host.block();
    got = synth.last();
    check("a new lane ends the old one's notes", got.size() == 1 && ump::isNoteOff(got[0].message));
    engine.collectGarbage();
}

void testSetup() {
    const Config c = parseConfig("# a comment\ninput = KeyboardEDO  \n  synth=Other # its own\nsynth-id = x.y\n");
    check("the setup file", c.input == "KeyboardEDO" && c.synth == "Other" && c.synthId == "x.y");
    check("defaults", parseConfig("").synth == "PlainOldSynth" && parseConfig("").input.empty());

    std::vector<uw::MidiSourceInfo> sources = {
        {"Midi Through:Port", "Midi Through", false, false, uw::MidiProtocol::Midi1},
        {"KeyLab:MIDI", "KeyLab 88", false, true, uw::MidiProtocol::Midi1},
        {"Wave:UMP", "Wave", false, true, uw::MidiProtocol::Midi2},
        {"KeyboardEDO", "KeyboardEDO", true, false, uw::MidiProtocol::Midi2},
    };
    check("chooses KeyboardEDO", chooseSource(sources, "") == 3);
    check("named, any case", chooseSource(sources, "keylab") == 1);
    check("none", chooseSource(sources, "none") == -1 && chooseSource(sources, "Nothing") == -1);
    sources.pop_back();
    check("else MIDI 2.0", chooseSource(sources, "") == 2);
    sources.pop_back();
    check("else hardware", chooseSource(sources, "") == 1);
}

void testState() {
    SavedState s;
    s.takes      = {{-3360, {1, 2, 3, 4, 5}}, {std::int64_t{1} << 40, {6}}};
    s.synthId    = "com.unlikelyware.plainoldsynth";
    s.synthState = {std::byte{9}, std::byte{8}};
    const auto  bytes = encodeState(s);
    SavedState  back;
    std::string error;
    const bool  ok = decodeState(bytes.data(), bytes.size(), back, error);
    check("the state round-trips", ok && back.takes.size() == 2 && back.takes[0].start == -3360 &&
                                       back.takes[0].clipFile == s.takes[0].clipFile &&
                                       back.takes[1].start == s.takes[1].start && back.takes[1].clipFile == s.takes[1].clipFile &&
                                       back.synthId == s.synthId && back.synthState == s.synthState,
          error);
    check("a cut-off state is refused", !decodeState(bytes.data(), bytes.size() - 1, back, error), error);
    const std::uint8_t junk[] = {'J', 'U', 'N', 'K', 1, 0, 0, 0};
    check("another's state is refused", !decodeState(junk, sizeof junk, back, error), error);
    const auto empty = encodeState(SavedState{});
    check("an empty one round-trips", decodeState(empty.data(), empty.size(), back, error) && back.takes.empty());
    check("unarmed by default", !back.armed);
    s.armed = true;
    const auto armedBytes = encodeState(s);
    check("the arm round-trips (version 3)", decodeState(armedBytes.data(), armedBytes.size(), back, error) && back.armed &&
                                                 back.takes.size() == 2, error);

    // P2's version 2: no flags, read unarmed.
    const std::uint8_t v2[] = {'U', 'M', 'P', 'P', 2, 0, 0, 0, 1, 0, 0, 0,   // one take
                               0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 7, 8,     // at 0, two bytes
                               1, 0, 0, 0, 'x', 0, 0, 0, 0};                 // synth "x", no state
    check("version 2 reads unarmed", decodeState(v2, sizeof v2, back, error) && back.takes.size() == 1 &&
                                         back.takes[0].clipFile.size() == 2 && back.synthId == "x" && !back.armed,
          error);

    // The spike's version 1: one clip from the song's start.
    const std::uint8_t v1[] = {'U', 'M', 'P', 'P', 1, 0, 0, 0, 3, 0, 0, 0, 7, 8, 9, 1, 0, 0, 0, 'x', 0, 0, 0, 0};
    check("version 1 reads as one take at 0", decodeState(v1, sizeof v1, back, error) && back.takes.size() == 1 &&
                                                  back.takes[0].start == 0 && back.takes[0].clipFile.size() == 3 &&
                                                  back.synthId == "x",
          error);
    const std::uint8_t v1empty[] = {'U', 'M', 'P', 'P', 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    check("version 1 without a clip has no takes", decodeState(v1empty, sizeof v1empty, back, error) && back.takes.empty(), error);
}

// --- P2 ---------------------------------------------------------------------------------

Tick beats(double b) { return std::llround(b * kTicksPerQuarter); }

Clip clipOf(Tick length, std::vector<ClipEvent> events) {
    Clip c;
    c.length = length;
    c.events = std::move(events);
    return c;
}

void testJumpWhileRecording() {
    FakeSynth    synth;
    PluginEngine engine;
    engine.setSynth(&synth);
    engine.activate(kRate, 4096);
    Host host(engine);

    // A loop from beat 8 to 8.1, recorded: each pass is a take.
    host.play(8.0, true);
    for (int pass = 0; pass < 3; ++pass) {
        host.transport.songBeats = 8.0;
        engine.onMidi(1, at(host.now - 5'000'000, ump::midi2NoteOn(0, 0, static_cast<std::uint8_t>(60 + pass), 0x8000)));
        for (int i = 0; i < 5; ++i) host.block();   // 0.1 beats
    }
    FinishedTake a, b;
    check("a jump while recording ends the take", engine.collectTake(a) && a.start == beats(8.0) &&
                                                      a.clip.length == beats(8.1) - beats(8.0),
          std::to_string(a.clip.length));
    check("and the next pass is its own take", engine.collectTake(b) && b.start == beats(8.0));
    check("the oldest is collected first", !a.clip.events.empty() && ump::noteNumber(a.clip.events[0].message) == 60 &&
                                               !b.clip.events.empty() && ump::noteNumber(b.clip.events[0].message) == 61);
    check("a held note ends where the pass did", a.clip.events.size() == 2 && ump::isNoteOff(a.clip.events[1].message) &&
                                                     a.clip.events[1].tick == a.clip.length);
    check("the third pass waited for a buffer: not recorded", engine.takeMissedCount() == 1 && !engine.recording());

    // The next jump, with buffers free, records again.
    host.transport.songBeats = 8.0;
    host.block();
    check("a buffer free: the next pass records", engine.recording());

    // Deactivating while recording ends the take (Bitwig, around a render).
    host.block();
    engine.deactivate();
    FinishedTake c;
    check("deactivating ends a take in progress", !engine.recording() && engine.collectTake(c) && c.start == beats(8.0));
}

void testTempoChangeInBlock() {
    FakeSynth    synth;
    PluginEngine engine;
    engine.setSynth(&synth);
    engine.activate(kRate, 4096);

    std::vector<float> l(kFrames), r(kFrames);
    float*             out[2] = {l.data(), r.data()};
    // From frame 240 the tempo is 60, from beat 1.01 (240 frames at 120).
    HostTransport t;
    t.playing = t.hasBeats = true;
    t.songBeats            = 1.0;
    TransportChange change;
    change.frame               = 240;
    change.transport           = t;
    change.transport.songBeats = 1.01;
    change.transport.tempo     = 60;

    // An event at beat 1.0125: 120 frames (2.5 ms) after the change at 60 bpm (at 120
    // it would be 60 frames after).
    engine.setEvents({{beats(1.0125), ump::midi2NoteOn(0, 0, 60, 0x8000)}});
    Block b;
    b.frames      = kFrames;
    b.transport   = t;
    b.changes     = &change;
    b.changeCount = 1;
    b.armed       = change.armed = true;
    b.now         = kNow;
    b.out         = out;
    b.outChannels = 2;
    engine.process(b);
    auto got = synth.last();
    check("a tempo change inside the block: played at the new tempo", got.size() == 1 && got[0].offset == 360,
          got.empty() ? "" : std::to_string(got[0].offset));

    // Recording across it: a live note at frame 360 is beat 2.0125.
    engine.setEvents({});
    t.recording = change.transport.recording = true;
    t.songBeats                              = 2.0;
    change.transport.songBeats               = 2.01;
    b.transport                              = t;
    b.now                                    = kNow + 10'000'000;
    engine.onMidi(1, at(b.now - 2'500'000, ump::midi2NoteOn(0, 0, 62, 0x8000)));   // frame 360
    engine.process(b);
    t.recording = false;
    t.songBeats = 2.02;
    b.transport = t;
    b.changes   = nullptr;
    b.changeCount = 0;
    b.now += 10'000'000;
    engine.process(b);
    FinishedTake take;
    check("recorded at the new tempo", engine.collectTake(take) && !take.clip.events.empty() &&
                                           take.start + take.clip.events[0].tick == beats(2.0125),
          take.clip.events.empty() ? "" : std::to_string(take.start + take.clip.events[0].tick - beats(2.0125)));
}

void testLane() {
    Lane lane;
    check("an empty take leaves nothing", !lane.addTake(0, Clip{}) && lane.takes().empty());

    // The older take: beats 0 to 4. Key 70 tuned at 0; a note 0.5 to 2.5
    // (under the newer take from 2), a note at 3 to 3.5, and a note on 64
    // ending at 2 exactly.
    lane.addTake(0, clipOf(beats(4), {
                         {0, pitch725(70, 0x7A000000)},
                         {beats(0.5), ump::midi2NoteOn(0, 0, 70, 0x8000)},
                         {beats(1.0), ump::midi2NoteOn(0, 0, 64, 0x8000)},
                         {beats(2.0), ump::midi2NoteOff(0, 0, 64)},
                         {beats(2.5), ump::midi2NoteOff(0, 0, 70)},
                         {beats(3.0), ump::midi2NoteOn(0, 0, 72, 0x8000)},
                         {beats(3.5), ump::midi2NoteOff(0, 0, 72)},
                     }));
    // The newer take: beats 2 to 3, a note on 64 from 2 to 2.75.
    lane.addTake(beats(2), clipOf(beats(1), {
                                       {0, ump::midi2NoteOn(0, 0, 64, 0x9000)},
                                       {beats(0.75), ump::midi2NoteOff(0, 0, 64)},
                                   }));
    check("takes are named in order", lane.takes().size() == 2 && lane.takes()[0].clip->name == "Take 1" &&
                                          lane.takes()[1].clip->name == "Take 2");

    const std::vector<ClipEvent> e = flatten(lane.takes());
    std::string                  shape;
    for (const ClipEvent& x : e) {
        char line[64];
        std::snprintf(line, sizeof line, "%.2f:%s%d ", static_cast<double>(x.tick) / kTicksPerQuarter,
                      ump::isNoteOn(x.message) ? "on" : ump::isNoteOff(x.message) ? "off" : isPitch725(x.message) ? "p" : "?",
                      ump::noteNumber(x.message));
        shape += line;
    }
    // 0 p70, 0.5 on70, 1 on64; at 2 the older's off64 and its cut off70
    // (both end the piece), then the newer's on64; 2.75 off64; at 3 the
    // older resumes: its p70 chased, on72; 3.5 off72. Its off70 at 2.5 is
    // under the newer take: gone.
    const std::string want =
        "0.00:p70 0.50:on70 1.00:on64 2.00:off64 2.00:off70 2.00:on64 2.75:off64 3.00:p70 3.00:on72 3.50:off72 ";
    check("the newest take plays where they overlap", shape == want, shape);

    // A take wholly under a newer one plays nothing, and is kept.
    Lane covered;
    covered.addTake(beats(1), clipOf(beats(1), {{0, ump::midi2NoteOn(0, 0, 60, 0x8000)}, {beats(0.5), ump::midi2NoteOff(0, 0, 60)}}));
    covered.addTake(0, clipOf(beats(4), {{beats(3), ump::midi2NoteOn(0, 0, 61, 0x8000)}, {beats(4), ump::midi2NoteOff(0, 0, 61)}}));
    const auto c = flatten(covered.takes());
    check("an older take wholly covered is silent but kept", covered.takes().size() == 2 && c.size() == 2 &&
                                                                  ump::noteNumber(c[0].message) == 61);

    // Takes that don't overlap both play whole, and a take's own end is inclusive.
    Lane apart;
    apart.addTake(0, clipOf(beats(1), {{0, ump::midi2NoteOn(0, 0, 60, 0x8000)}, {beats(1), ump::midi2NoteOff(0, 0, 60)}}));
    apart.addTake(beats(1), clipOf(beats(1), {{0, ump::midi2NoteOn(0, 0, 60, 0x8000)}, {beats(1), ump::midi2NoteOff(0, 0, 60)}}));
    const auto d = flatten(apart.takes());
    check("back to back: the first's off comes before the second's on", d.size() == 4 && ump::isNoteOff(d[1].message) &&
                                                                          ump::isNoteOn(d[2].message) && d[1].tick == d[2].tick);
}

void testLaneKeeper() {
    FakeSynth    synth;
    PluginEngine engine;
    engine.setSynth(&synth);
    engine.activate(kRate, 4096);
    Host       host(engine);
    int        added = 0;
    LaneKeeper keeper(engine, [&](const ClipRef&) { ++added; });

    // Record a note at beat 4.01, without the lane thread: pump() by hand.
    host.play(4.0, true);
    engine.onMidi(1, at(host.now - 5'000'000, ump::midi2NoteOn(0, 0, 60, 0x8000)));
    host.block();
    host.block();
    host.transport.recording = false;
    host.block();
    check("pump() adds the take to the lane", keeper.pump() && added == 1 && keeper.takes().size() == 1 &&
                                                  keeper.takes()[0].start == beats(4.0));
    check("and marks the state dirty, once", keeper.takeDirty() && !keeper.takeDirty());

    // Played back from where it was recorded.
    host.play(4.0);
    host.block();
    auto got = synth.last();
    check("the take plays back where it was played", got.size() == 1 && ump::isNoteOn(got[0].message) && got[0].offset == 240,
          got.empty() ? "" : std::to_string(got[0].offset));

    // With the lane thread: a take lands without anyone calling pump().
    keeper.start();
    host.play(6.0, true);
    engine.onMidi(1, at(host.now - 5'000'000, ump::midi2NoteOn(0, 0, 62, 0x8000)));
    host.block();
    host.transport.recording = false;
    host.block();
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (keeper.takes().size() < 2 && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    check("the lane thread collects a take by itself", keeper.takes().size() == 2 && added == 2);
    keeper.stop();

    // A loaded state replaces the lane.
    ClipRef loaded;
    loaded.start = beats(1);
    loaded.clip  = std::make_shared<Clip>(clipOf(beats(1), {{0, ump::midi2NoteOn(0, 0, 65, 0x8000)}, {beats(0.5), ump::midi2NoteOff(0, 0, 65)}}));
    keeper.setTakes({loaded});
    check("setTakes() replaces the lane", keeper.takes().size() == 1 && keeper.takes()[0].clip->name == "Take 1");
    host.play(1.0);
    host.block();
    got = synth.last();
    check("and the engine plays it", got.size() == 1 && ump::noteNumber(got[0].message) == 65);
}

} // namespace

// The arm (the Record parameter): a take records only while the host records
// and the instance is armed.
void testArm() {
    FakeSynth    synth;
    PluginEngine engine;
    engine.setSynth(&synth);
    engine.activate(kRate, 4096);
    Host host(engine);

    // Unarmed: the host records, this instance doesn't, and plays its lane on.
    engine.setEvents({{beats(4.01), ump::midi2NoteOn(0, 0, 72, 0x8000)}, {beats(4.5), ump::midi2NoteOff(0, 0, 72)}});
    host.armed = false;
    host.play(4.0, true);
    engine.onMidi(1, at(host.now - 5'000'000, ump::midi2NoteOn(0, 0, 60, 0x8000)));
    host.block();
    auto got = synth.last();
    bool lanePlayed = false;
    for (const auto& e : got) lanePlayed = lanePlayed || (ump::isNoteOn(e.message) && ump::noteNumber(e.message) == 72);
    check("unarmed, the host recording doesn't record", !engine.recording());
    check("and the lane plays on", lanePlayed);
    check("live input still plays through", got.size() >= 2);
    host.block();
    FinishedTake none;
    check("no take", !engine.collectTake(none));

    // Armed inside a block (a param event at frame 240): punched in there.
    engine.setEvents({});
    engine.onMidi(1, at(host.now - 5'000'000, ump::midi2NoteOff(0, 0, 60)));
    host.block();
    std::vector<float> l(kFrames), r(kFrames);
    float*             out[2] = {l.data(), r.data()};
    TransportChange    arm;
    arm.frame        = 240;
    arm.hasTransport = false;   // an arm change: the position goes on from frame 0's
    arm.armed        = true;
    Block b;
    b.frames      = kFrames;
    b.transport   = host.transport;   // beat 4.06
    b.armed       = false;
    b.changes     = &arm;
    b.changeCount = 1;
    b.now         = host.now;
    b.out         = out;
    b.outChannels = 2;
    engine.onMidi(1, at(host.now - 2'500'000, ump::midi2NoteOn(0, 0, 64, 0x8000)));   // frame 360
    engine.process(b);
    host.now += 10'000'000;
    host.transport.songBeats += kBeatsPerBlock;
    check("armed inside the block: recording", engine.recording());
    host.armed = true;
    host.block();
    host.armed = false;   // disarmed: the take ends, the host still recording
    host.block();
    check("disarmed: stopped", !engine.recording());
    FinishedTake take;
    check("the take starts where it was armed (beat 4.07)", engine.collectTake(take) && take.start == beats(4.07),
          std::to_string(take.start));
    check("with the note played after it", !take.clip.events.empty() && ump::noteNumber(take.clip.events[0].message) == 64 &&
                                               take.clip.events[0].tick == beats(4.075) - beats(4.07),
          take.clip.events.empty() ? "" : std::to_string(take.clip.events[0].tick));
    check("and ends where it was disarmed", take.clip.length == beats(4.1) - beats(4.07), std::to_string(take.clip.length));
}

// P3: what the plugin's window does to the lane, without the window.
void testEditor() {
    FakeSynth    synth;
    PluginEngine engine;
    engine.setSynth(&synth);
    engine.activate(kRate, 4096);
    Host       host(engine);
    LaneKeeper keeper(engine);

    // Two takes: Take 1 at beat 4, beats 4 to 8, a note 60 at its beat 1;
    // Take 2 at beat 6, beats 6 to 7, a note 62.
    ClipRef one, two;
    one.start = beats(4);
    one.clip  = std::make_shared<Clip>(clipOf(beats(4), {{beats(1), ump::midi2NoteOn(0, 0, 60, 0x8000)},
                                                          {beats(1.5), ump::midi2NoteOff(0, 0, 60)}}));
    two.start = beats(6);
    two.clip  = std::make_shared<Clip>(clipOf(beats(1), {{0, ump::midi2NoteOn(0, 0, 62, 0x8000)},
                                                          {beats(0.5), ump::midi2NoteOff(0, 0, 62)}}));
    keeper.setTakes({one, two});
    LaneKeeper::View v = keeper.view();
    check("a loaded lane has nothing to undo", v.undoLabel.empty() && v.redoLabel.empty() && v.takes.size() == 2);
    const ClipRefId first = v.takes[0].id, second = v.takes[1].id;

    // What's heard of Take 1 and what's covered: beats 4 to 6 and 7 to 8.
    const auto spans = visibleSpans(v.takes, 0);
    check("an older take's heard spans skip the newer one",
          spans.size() == 2 && spans[0].start == beats(4) && spans[0].end == beats(6) && spans[1].start == beats(7) &&
              spans[1].end == beats(8));
    check("the newest is heard whole", visibleSpans(v.takes, 1).size() == 1);

    // The grid in song time: Take 1 starts on bar 2 (beat 4), so origin 0
    // from there; a take at beat 5.5 sits 1.5 beats into its bar.
    GridModel grid;
    grid.origin = beats(5.5) - beats(4);
    grid.showClip(v.takes[0].clip);
    check("the grid shows a note at its song beat", grid.notes.size() == 1 && grid.notes[0].start == beats(1) + beats(1.5));
    check("and is long enough for the take from its bar", grid.endTick() >= beats(1.5) + beats(4));

    // An edit through the grid: the note a beat earlier (a beat of
    // sixteenths), to beat 4, given to the lane as the window does.
    const std::uint64_t before = keeper.version();
    grid.release();
    grid.origin = 0;
    grid.showClip(v.takes[0].clip);
    grid.editClip = [&](const std::string& label, const std::vector<NoteChange>& changes, const std::vector<NewNote>& added) {
        return keeper.editTake(first, label, changes, added).edit;
    };
    grid.select(0);
    grid.nudge(-GridModel::kStep * 4, 0);
    check("the grid commits the move to the lane", grid.commit());
    v = keeper.view();
    check("as one step, Move note", v.undoLabel == "Move note" && keeper.version() != before, v.undoLabel);
    check("the take's clip is new, its id kept", v.takes[0].id == first && v.takes[0].clip != one.clip &&
                                                    v.takes[0].clip->name == "Take 1");
    check("and the host's state is dirty", keeper.takeDirty());
    keeper.pump();   // the engine takes what plays
    synth.blocks.clear();
    host.play(3.98);
    host.block();
    host.block();
    bool heard = false;
    for (const auto& block : synth.blocks)
        for (const auto& g : block) heard |= ump::isNoteOn(g.message) && ump::noteNumber(g.message) == 60;
    check("the moved note is heard at beat 4", heard);

    // A note moved before the take (the grid's origin reaches back): the
    // take starts earlier, the note where it was put.
    const auto early = keeper.editTake(first, "Move note", {{0, false, -beats(1), beats(0.5), 60 << 9, 0x8000}}, {});
    v                = keeper.view();
    check("a note before the take moves the take's start", early.edit.clip && early.start == beats(3) &&
                                                               v.takes[0].start == beats(3),
          std::to_string(early.start));
    const auto notes = early.edit.clip ? notesOf(early.edit.clip->events, early.edit.clip->length) : std::vector<NoteSpan>{};
    check("and the note is at beat 3 of the song", notes.size() == 1 && v.takes[0].start + notes[0].start == beats(3));

    // Undo and redo.
    check("undo", keeper.undo());
    v = keeper.view();
    check("brings back the take's start and clip", v.takes[0].start == beats(4) && v.redoLabel == "Move note");
    check("redo", keeper.redo() && keeper.view().takes[0].start == beats(3));

    // Bring to front, delete.
    check("bring an older take to the front", keeper.bringToFront(first));
    v = keeper.view();
    check("it's the newest now", v.takes.back().id == first && v.undoLabel == "Bring to front");
    check("the newest can't be brought to the front", !keeper.bringToFront(first));
    check("delete a take", keeper.deleteTake(second) && keeper.view().takes.size() == 1);
    check("undo the delete", keeper.undo() && keeper.view().takes.size() == 2 && keeper.view().takes[0].id == second);

    // A recorded take is an undo step too.
    host.play(10.0, true);
    engine.onMidi(1, at(host.now - 5'000'000, ump::midi2NoteOn(0, 0, 64, 0x8000)));
    host.block();
    host.transport.recording = false;
    host.block();
    keeper.pump();
    v = keeper.view();
    check("a recorded take is a step", v.takes.size() == 3 && v.undoLabel == "Record take", v.undoLabel);
    check("which undoes", keeper.undo() && keeper.view().takes.size() == 2);

    // The playhead: where the host is.
    host.play(2.0);
    host.block();
    check("the engine says where the host is, and that it plays", engine.hostPlaying() &&
                                                                       std::llabs(engine.position() - beats(2.02)) <= 1,
          std::to_string(engine.position()));
    host.transport.playing = false;
    host.block();
    check("stopped", !engine.hostPlaying());
}

int main() {
    testLiveInput();
    testMidi1Synth();
    testRecording();
    testPlayback();
    testSetup();
    testState();
    testJumpWhileRecording();
    testTempoChangeInBlock();
    testLane();
    testLaneKeeper();
    testArm();
    testEditor();
    std::printf("%s\n", failures == 0 ? "all passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
