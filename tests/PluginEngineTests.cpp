// Milestone P1, the plugin spike (docs/plugin-version.md): the engine inside
// the plugin (src/Plugin/SpikeEngine.h), driven as a host would drive it,
// with a fake synth that only keeps the events it's given. Live input at
// its sample offsets, MIDI 1.0 upconverted, a MIDI 1.0 synth downconverted,
// offline render ignoring live input; recording from the host's position;
// playback from it, with the chase on a jump and notes ended on stop; and
// the setup file and the saved state (src/Plugin/PluginSetup.h).
// Silent: no MIDI hardware, no audio, no window.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "App/ClipFile.h"
#include "Plugin/PluginSetup.h"
#include "Plugin/SpikeEngine.h"
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
    explicit Host(SpikeEngine& e) : engine(e) {}
    SpikeEngine&       engine;
    std::vector<float> left = std::vector<float>(kFrames), right = std::vector<float>(kFrames);
    float*             out[2] = {left.data(), right.data()};
    uw::Nanoseconds    now    = kNow;
    HostTransport      transport;

    void block() {
        Block b;
        b.frames      = kFrames;
        b.transport   = transport;
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
    SpikeEngine engine;
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
    SpikeEngine engine;
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
    SpikeEngine engine;
    engine.setSynth(&synth);
    engine.activate(kRate, 4096);
    Host host(engine);

    Clip none;
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

    Clip take;
    check("the take is collected", engine.collectTake(take));
    // 240 frames at 120 bpm and 48 kHz: 0.01 beats, 403 ticks.
    const Tick start = 4 * kTicksPerQuarter + 403;
    check("four events: two notes, the held one ended", take.events.size() == 4, std::to_string(take.events.size()));
    if (take.events.size() == 4) {
        check("its ticks are the song's", take.events[0].tick == start, std::to_string(take.events[0].tick));
        check("the off a block and a bit later", take.events[1].tick == start + 2 * 806 + 1, std::to_string(take.events[1].tick));
        const Tick end = std::llround(stopBeats * kTicksPerQuarter);
        check("a held note ends with the take", ump::isNoteOff(take.events[3].message) && take.events[3].tick == end);
        check("the clip runs to the take's end", take.length == end);
    }
    check("collected once", !engine.collectTake(take));

    // Through a MIDI Clip File and back, exactly (the state's clip).
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
    SpikeEngine engine;
    engine.setSynth(&synth);
    engine.activate(kRate, 4096);
    Host host(engine);

    Clip clip;
    clip.length = 2 * kTicksPerQuarter;
    clip.events.push_back({0, pitch725(60, 0x7A000000)});
    clip.events.push_back({std::llround(1.01 * kTicksPerQuarter), ump::midi2NoteOn(0, 0, 60, 0x8000)});
    clip.events.push_back({std::llround(1.50 * kTicksPerQuarter), ump::midi2NoteOff(0, 0, 60)});
    engine.setClip(clip);

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
    Clip take;
    engine.collectTake(take);

    // A new clip while playing: the old one's notes end.
    host.play(1.0);
    host.block();
    host.block();
    engine.setClip(Clip{});
    host.block();
    got = synth.last();
    check("a new clip ends the old one's notes", got.size() == 1 && ump::isNoteOff(got[0].message));
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
    s.clipFile   = {1, 2, 3, 4, 5};
    s.synthId    = "com.unlikelyware.plainoldsynth";
    s.synthState = {std::byte{9}, std::byte{8}};
    const auto  bytes = encodeState(s);
    SavedState  back;
    std::string error;
    check("the state round-trips", decodeState(bytes.data(), bytes.size(), back, error) && back.clipFile == s.clipFile &&
                                       back.synthId == s.synthId && back.synthState == s.synthState);
    check("a cut-off state is refused", !decodeState(bytes.data(), bytes.size() - 1, back, error), error);
    const std::uint8_t junk[] = {'J', 'U', 'N', 'K', 1, 0, 0, 0};
    check("another's state is refused", !decodeState(junk, sizeof junk, back, error), error);
    const auto empty = encodeState(SavedState{});
    check("an empty one round-trips", decodeState(empty.data(), empty.size(), back, error) && back.clipFile.empty());
}

} // namespace

int main() {
    testLiveInput();
    testMidi1Synth();
    testRecording();
    testPlayback();
    testSetup();
    testState();
    std::printf("%s\n", failures == 0 ? "all passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
