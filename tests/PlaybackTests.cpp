// Playback to MIDI outputs (src/App/Player.h, docs/playback.md) over a fake
// MIDI output and a fake clock: the transport's loop and runs, the output
// choices, a track's clips scheduled ahead at their times, stop and locate
// ending notes, a MIDI 1.0 device downconverted, mute and solo, the loop
// wrapping, Pitch 7.25 chased on play, locate and wrap, playing through to the
// touched track, a tempo change, and a device that comes later. Silent: the
// fake output sends nothing anywhere.

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "App/AudioEngine.h"
#include "App/MainLoop.h"
#include "App/MidiInputs.h"
#include "App/MidiOutputs.h"
#include "App/TrackController.h"
#include "uw/ump/Ump.h"

namespace {

int failures = 0;

void check(const char* what, bool ok, const std::string& detail = {}) {
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str());
    if (!ok) ++failures;
}

uw::Nanoseconds fakeNow = 1'000'000'000;

// A MIDI 2.0 synth, a MIDI 1.0 one, and Umpire's own input (not offered). It
// schedules as the ALSA sequencer does: what's sent for later waits, and
// cancelling drops what hasn't reached its time.
class FakeOutput final : public uw::MidiOutput {
public:
    struct Sent {
        uw::MidiDestinationHandle handle = 0;
        std::string               device;
        uw::MidiMessage           message;
        uw::Nanoseconds           when = 0;
    };
    std::vector<uw::MidiDestinationInfo> destinations{
        {"Synth2:in", "Synth2", false, true, uw::MidiProtocol::Midi2},
        {"Synth1:in", "Synth1", false, false, uw::MidiProtocol::Midi1},
        {"Umpire MIDI 2.0:MIDI 2.0 In", "MIDI 2.0 In", true, false, uw::MidiProtocol::Midi2}};
    std::map<uw::MidiDestinationHandle, std::string> opened;
    std::vector<Sent>                                sent;   // in the order sent
    uw::MidiDestinationHandle                        next      = 1;
    std::uint64_t                                    announced = 0;

    std::vector<uw::MidiDestinationInfo> enumerate() override { return destinations; }
    uw::MidiDestinationHandle open(const std::string& id) override {
        for (const auto& d : destinations)
            if (d.id == id) return opened[next] = id, next++;
        return uw::kInvalidMidiDestination;
    }
    uw::MidiDestinationHandle openVirtual(const std::string&, uw::MidiProtocol, std::int32_t) override { return 0; }
    void                      close(uw::MidiDestinationHandle h) override {
        cancelScheduled(h);
        opened.erase(h);
    }
    uw::MidiProtocol protocol(uw::MidiDestinationHandle h) const override {
        const auto it = opened.find(h);
        for (const auto& d : destinations)
            if (it != opened.end() && d.id == it->second) return d.protocol;
        return uw::MidiProtocol::Midi1;
    }
    bool send(uw::MidiDestinationHandle h, const uw::MidiMessage* m, std::size_t n) override {
        return sendAt(h, m, n, fakeNow);
    }
    bool sendAt(uw::MidiDestinationHandle h, const uw::MidiMessage* m, std::size_t n, uw::Nanoseconds when) override {
        if (opened.count(h) == 0) return false;
        for (std::size_t i = 0; i < n; ++i) sent.push_back(Sent{h, opened[h], m[i], std::max(when, fakeNow)});
        return true;
    }
    bool schedules(uw::MidiDestinationHandle) const override { return true; }
    void cancelScheduled(uw::MidiDestinationHandle h) override {
        std::erase_if(sent, [h](const Sent& s) { return s.handle == h && s.when > fakeNow; });
    }
    std::uint64_t changes() const override { return announced; }
    bool          sync(uw::MidiDestinationHandle h) override {
        for (const auto& d : destinations)
            if (opened.count(h) != 0 && d.id == opened[h]) return true;
        return false;
    }
    const char* name() const override { return "fake"; }

    // What reached `device` by now (or will, if `all`), in time order.
    std::vector<Sent> at(const std::string& device, bool all = false) const {
        std::vector<Sent> out;
        for (const Sent& s : sent)
            if (s.device == device && (all || s.when <= fakeNow)) out.push_back(s);
        std::stable_sort(out.begin(), out.end(), [](const Sent& a, const Sent& b) { return a.when < b.when; });
        return out;
    }
};

// A plugin that records every note it's given, with the sample offset
// AudioEngine worked out for it: how the test checks sample-accurate timing
// (App/AudioEngine.h) without a real CLAP plugin or audio device.
class FakePlugin final : public uw::HostedPlugin {
public:
    struct Received {
        std::uint32_t   sampleOffset;
        uw::MidiMessage midi;
    };
    std::vector<Received> received;
    uw::PluginDescriptor   desc;
    bool                   activated = false;

    explicit FakePlugin(uw::PluginDescriptor d) : desc(std::move(d)) {}

    const uw::PluginDescriptor&    descriptor() const override { return desc; }
    uw::PortLayout                 portLayout() const override {
        uw::PortLayout layout;
        layout.noteInputs.push_back(
            uw::NotePortInfo{"in", uw::NoteDialect::Midi2 | uw::NoteDialect::Midi, uw::NoteDialect::Midi2});
        layout.audioOutputs.push_back(uw::AudioPortInfo{"out", 2, true});
        return layout;
    }
    std::vector<uw::ParameterInfo> parameters() const override { return {}; }
    bool                           activate(double, std::uint32_t) override { return activated = true; }
    void                           deactivate() override { activated = false; }
    void process(const uw::ProcessContext& ctx) noexcept override {
        for (std::uint32_t i = 0; i < ctx.eventsIn.count; ++i) {
            const uw::Event& e = ctx.eventsIn.events[i];
            if (e.type == uw::EventType::Midi) received.push_back({e.sampleOffset, e.midi});
        }
        for (std::uint32_t c = 0; c < ctx.audioOut.channelCount; ++c)
            for (std::uint32_t f = 0; f < ctx.frameCount; ++f) ctx.audioOut.channels[c][f] = 0.0f;
    }
    void   stopProcessing() noexcept override {}
    double paramValue(std::uint32_t) const override { return 0.0; }
    void   flushParams(const uw::EventBlock&, uw::EventSink*) override {}
    bool   saveState(std::vector<std::byte>&) const override { return true; }
    bool   loadState(const std::byte*, std::size_t) override { return true; }
    void   idle() override {}
};

class FakePluginFactory final : public uw::PluginFactory {
public:
    FakePlugin* lastCreated = nullptr;

    uw::PluginFormat                  format() const override { return uw::PluginFormat::Clap; }
    std::vector<uw::PluginDescriptor> scan() override {
        uw::PluginDescriptor d;
        d.format = uw::PluginFormat::Clap;
        d.id     = "fake-synth";
        d.path   = "fake://synth";
        d.name   = "Fake Synth";
        d.kind   = uw::PluginKind::Instrument;
        return {d};
    }
    std::unique_ptr<uw::HostedPlugin> create(const uw::PluginDescriptor& d, uw::HostServices&) override {
        auto plugin  = std::make_unique<FakePlugin>(d);
        lastCreated  = plugin.get();
        return plugin;
    }
};

class NoInput final : public uw::MidiInput {
public:
    void setProtocolPreference(uw::MidiProtocol) override {}
    void setSink(uw::MidiSink*) override {}
    std::vector<uw::MidiSourceInfo> enumerate() override { return {}; }
    uw::MidiSourceHandle open(const std::string&) override { return uw::kInvalidMidiSource; }
    uw::MidiSourceHandle openVirtual(const std::string&, uw::MidiProtocol) override { return 7; }
    void                 close(uw::MidiSourceHandle) override {}
    const char*          name() const override { return "none"; }
};

namespace ump = uw::ump;
using umpire::ClipEvent;
using umpire::Tick;
using umpire::kTicksPerQuarter;

constexpr Tick            kQ    = kTicksPerQuarter;
constexpr uw::Nanoseconds kBeat = 500'000'000;   // at 120
constexpr uw::Nanoseconds kMs   = 1'000'000;

uw::MidiMessage on(std::uint8_t key) { return ump::midi2NoteOn(0, 0, key, 0x8000); }
uw::MidiMessage off(std::uint8_t key) { return ump::midi2NoteOff(0, 0, key, 0); }
uw::MidiMessage pitch725(std::uint8_t key, std::uint32_t value) {
    return uw::MidiMessage{{0x40000000u | (std::uint32_t{key} << 8) | 3u, value, 0, 0}, 2};
}
uw::MidiMessage perNoteReset(std::uint8_t key) {
    return uw::MidiMessage{{0x40F00000u | (std::uint32_t{key} << 8) | 0x01u, 0, 0, 0}, 2};
}

bool same(const uw::MidiMessage& a, const uw::MidiMessage& b) {
    if (a.wordCount != b.wordCount) return false;
    for (int i = 0; i < a.wordCount; ++i)
        if (a.words[i] != b.words[i]) return false;
    return true;
}

bool isOn(const FakeOutput::Sent& s, int key) { return ump::isNoteOn(s.message) && ump::noteNumber(s.message) == key; }
bool isOff(const FakeOutput::Sent& s, int key) { return ump::isNoteOff(s.message) && ump::noteNumber(s.message) == key; }

std::size_t count(const std::vector<FakeOutput::Sent>& sent, bool (*what)(const FakeOutput::Sent&, int), int key) {
    return static_cast<std::size_t>(std::count_if(sent.begin(), sent.end(), [&](const auto& s) { return what(s, key); }));
}

// Ticks the controller every 10 ms for `ms`.
void run(umpire::TrackController& c, int ms) {
    for (int i = 0; i < ms / 10; ++i) {
        fakeNow += 10 * kMs;
        c.tick();
    }
}

std::string describe(const std::vector<FakeOutput::Sent>& sent, uw::Nanoseconds from) {
    std::string out;
    for (const auto& s : sent) {
        char text[64];
        std::snprintf(text, sizeof text, "%s%08X@%lld", out.empty() ? "" : " ", s.message.words[0],
                      static_cast<long long>((s.when - from) / kMs));
        out += text;
    }
    return out;
}

} // namespace

int main() {
    using namespace umpire;

    // --- the transport: loop and runs -------------------------------------------------
    {
        Transport t;
        t.setLoop(true, 4 * kQ, 8 * kQ, 0);
        t.locate(6 * kQ, 0);
        t.play(0);
        const auto runs = t.runs(t.rawAt(0), t.rawAt(4 * kBeat));
        check("runs: from beat 6 to the loop's end, then back to its start",
              runs.size() == 2 && runs[0] == Transport::Run{6 * kQ, 8 * kQ, 6 * kQ, false} &&
                  runs[1] == Transport::Run{4 * kQ, 6 * kQ, 8 * kQ, true});
        check("the position wraps: beat 9 is beat 5", t.tickAt(3 * kBeat) == 5 * kQ);
        check("and a run's ticks have their times", t.timeOf(runs[1], 4 * kQ) == 2 * kBeat);
        t.setRecording(true, 3 * kBeat);
        check("no wrap while recording", t.tickAt(6 * kBeat) == 8 * kQ && t.runs(t.rawAt(3 * kBeat), t.rawAt(6 * kBeat)).size() == 1);
        Transport past;
        past.setLoop(true, 0, 4 * kQ, 0);
        past.locate(5 * kQ, 0);
        past.play(0);
        check("a playhead past the loop's end plays on", past.tickAt(2 * kBeat) == 7 * kQ);
    }

    NoInput     none;
    MidiInputs  inputs(&none);
    FakeOutput  fake;
    MidiOutputs outputs(&fake, {"Umpire", "Umpire MIDI 2.0"});
    check("the outputs: hardware first, and not Umpire's own input",
          outputs.choices().size() == 2 && outputs.choices()[0].id == "Synth2:in" && outputs.choices()[1].id == "Synth1:in");

    FakePluginFactory pluginFactory;
    MainLoop          mainLoop;
    AudioEngine       engine(nullptr, pluginFactory, mainLoop);   // no backend: driven by hand, offline-style, below
    engine.instruments().rescan();
    TrackController   c(inputs, outputs, engine, [] { return fakeNow; });
    const TrackId keys = c.addTrack();
    check("a track has no output to start with: 2 MIDI devices and the one fake plugin",
          c.view().rows[0].outputLabel == "No output" && c.view().outputs.size() == 3);
    c.setOutput(keys, "Synth2:in");
    c.tick();
    check("a track's output: an undoable command, shown on its row",
          c.session().outputOf(keys) != nullptr && c.view().rows[0].outputLabel == "Synth2" &&
              !c.view().rows[0].outputMissing && c.view().undoLabel == "Set output");

    // A clip made by recording: notes 60 on beats 0-1 and 62 on beats 1-2,
    // recorded live (which plays through to the output too).
    c.setArmed(keys, true);
    c.record();
    const uw::Nanoseconds take = fakeNow;
    c.receive(0, on(60));
    c.tick();
    check("playing goes through to the touched track's output, now",
          fake.at("Synth2:in").size() == 1 && isOn(fake.at("Synth2:in")[0], 60) && fake.at("Synth2:in")[0].when == take);
    fakeNow = take + kBeat;
    c.receive(0, off(60));
    c.receive(0, on(62));
    c.tick();
    fakeNow = take + 2 * kBeat;
    c.receive(0, off(62));
    c.tick();
    fakeNow = take + 4 * kBeat;
    c.stop();
    c.setArmed(keys, false);
    check("the take is a clip", c.tracks()[0].clips.size() == 1);
    fake.sent.clear();

    // --- playing a clip ----------------------------------------------------------------
    c.locate(0);
    c.play();
    const uw::Nanoseconds t0 = fakeNow;
    c.tick();
    {
        const auto ahead = fake.at("Synth2:in", true);
        check("playing sends ahead, not beyond the lookahead",
              ahead.size() == 1 && isOn(ahead[0], 60) && ahead[0].when == t0, describe(ahead, t0));
    }
    run(c, 1200);
    {
        const auto got = fake.at("Synth2:in");
        check("each note at its time, once",
              got.size() == 4 && isOn(got[0], 60) && got[0].when == t0 && isOff(got[1], 60) &&
                  got[1].when == t0 + kBeat && isOn(got[2], 62) && got[2].when == t0 + kBeat && isOff(got[3], 62) &&
                  got[3].when == t0 + 2 * kBeat,
              describe(got, t0));
    }
    // Stopping mid-note: the note ends now, and what was ahead is dropped.
    c.locate(0);
    fake.sent.clear();
    run(c, 250);
    c.stop();
    {
        const auto all = fake.at("Synth2:in", true);
        check("stop ends a note sounding, now, and drops what was ahead",
              all.size() == 2 && isOn(all[0], 60) && isOff(all[1], 60) && all[1].when == fakeNow, describe(all, t0));
    }

    // --- a locate while playing ------------------------------------------------------------
    c.locate(0);
    c.play();
    fake.sent.clear();
    run(c, 300);
    c.locate(kQ + kQ / 2);   // mid 62
    const uw::Nanoseconds jumped = fakeNow;
    run(c, 1000);
    {
        const auto got = fake.at("Synth2:in");
        check("a locate ends what's sounding, and plays on from there",
              got.size() == 3 && isOn(got[0], 60) && isOff(got[1], 60) && got[1].when == jumped && isOff(got[2], 62) &&
                  got[2].when == jumped + kBeat / 2,
              describe(got, jumped));
    }
    c.stop();

    // --- a MIDI 1.0 device -------------------------------------------------------------------
    c.setOutput(keys, "Synth1:in");
    c.locate(0);
    c.play();
    fake.sent.clear();
    run(c, 600);
    {
        const auto got = fake.at("Synth1:in");
        check("a MIDI 1.0 device gets MIDI 1.0",
              got.size() >= 2 && ump::messageType(got[0].message) == ump::MessageType::Midi1ChannelVoice &&
                  isOn(got[0], 60) && fake.at("Synth2:in", true).empty());
    }
    c.stop();
    c.undo();
    check("undo puts the output back", c.session().outputOf(keys) != nullptr && *c.session().outputOf(keys) == "Synth2:in");

    // --- mute and solo ---------------------------------------------------------------------
    c.locate(0);
    c.play();
    fake.sent.clear();
    run(c, 300);
    c.setMuted(keys, true);
    const uw::Nanoseconds muted = fakeNow;
    run(c, 1000);
    {
        const auto got = fake.at("Synth2:in", true);
        check("muting while playing ends its notes and plays nothing more",
              got.size() == 2 && isOff(got[1], 60) && got[1].when <= muted + 10 * kMs, describe(got, muted));
    }
    c.setMuted(keys, false);
    const TrackId other = c.addTrack();
    c.setSoloed(other, true);
    c.locate(0);
    fake.sent.clear();
    run(c, 1000);
    check("soloing another track silences this one", fake.at("Synth2:in", true).empty());
    c.setSoloed(other, false);
    c.select(keys);
    c.stop();

    // --- the loop --------------------------------------------------------------------------
    // Beats 0 to 1.5: 60 plays, 62 starts, and the wrap ends it.
    c.setLoop(0, kQ + kQ / 2);
    check("set loop: a command, the loop on", c.session().looping && c.view().transport.looping &&
                                                  c.view().transport.loopEnd == kQ + kQ / 2 && c.view().undoLabel == "Set loop");
    c.locate(0);
    c.play();
    const uw::Nanoseconds l0 = fakeNow;
    fake.sent.clear();
    run(c, 1600);
    {
        const auto got = fake.at("Synth2:in");
        const uw::Nanoseconds wrap = l0 + 3 * kBeat / 2;
        const bool ok = got.size() >= 6 && isOn(got[0], 60) && isOff(got[1], 60) && isOn(got[2], 62) &&
                        isOff(got[3], 62) && got[3].when == wrap && isOn(got[4], 60) && got[4].when == wrap &&
                        isOff(got[5], 60) && got[5].when == wrap + kBeat;
        check("the loop wraps: notes sounding end there, and it plays from the loop's start", ok, describe(got, l0));
        check("the playhead wraps too", c.position() < kQ + kQ / 2);
    }
    c.setLooping(false);
    check("loop off: a command", !c.session().looping && c.view().undoLabel == "Loop off");
    c.stop();

    // --- Pitch 7.25 chased ------------------------------------------------------------------
    // A clip of its own: key 61 tuned at 0, reset at beat 2; key 63 tuned at
    // beat 3; a note on 61 at beat 1.
    {
        const TrackId tuned = c.addTrack();
        c.setOutput(tuned, "Synth2:in");
        c.setSoloed(tuned, true);
        c.setArmed(tuned, true);
        c.locate(0);
        c.record();
        const uw::Nanoseconds r0 = fakeNow;
        c.receive(0, pitch725(61, 0x7A000000));
        c.tick();
        fakeNow = r0 + kBeat;
        c.receive(0, on(61));
        c.tick();
        fakeNow = r0 + kBeat + kBeat / 2;
        c.receive(0, off(61));
        c.tick();
        fakeNow = r0 + 2 * kBeat;
        c.receive(0, perNoteReset(61));
        c.tick();
        fakeNow = r0 + 3 * kBeat;
        c.receive(0, pitch725(63, 0x7C000000));
        c.tick();
        fakeNow = r0 + 4 * kBeat;
        c.stop();
        c.setArmed(tuned, false);
        fake.sent.clear();

        const auto chasedAt = [&](Tick at) {
            fake.sent.clear();
            c.locate(at);
            c.play();
            const uw::Nanoseconds t = fakeNow;
            c.tick();
            std::vector<FakeOutput::Sent> got;
            for (const auto& s : fake.at("Synth2:in")) if (s.when == t && !ump::isNoteOn(s.message)) got.push_back(s);
            c.stop();
            return got;
        };
        auto got = chasedAt(kQ);
        check("play at beat 1: key 61's Pitch 7.25, and a reset for 63, not tuned yet",
              got.size() == 2 && same(got[0].message, pitch725(61, 0x7A000000)) && same(got[1].message, perNoteReset(63)),
              describe(got, fakeNow));
        got = chasedAt(2 * kQ + kQ / 2);
        check("at beat 2.5: both reset", got.size() == 2 && same(got[0].message, perNoteReset(61)) &&
                                             same(got[1].message, perNoteReset(63)), describe(got, fakeNow));
        got = chasedAt(3 * kQ + kQ / 2);
        check("at beat 3.5: 63's tuning", got.size() == 2 && same(got[1].message, pitch725(63, 0x7C000000)));

        // A loop over beats 1 to 3.5: the wrap chases at beat 1.
        c.setLoop(kQ, 3 * kQ + kQ / 2);
        c.locate(3 * kQ);
        c.play();
        const uw::Nanoseconds p0 = fakeNow;
        fake.sent.clear();
        run(c, 600);
        const uw::Nanoseconds wrap = p0 + kBeat / 2;
        std::vector<FakeOutput::Sent> atWrap;
        for (const auto& s : fake.at("Synth2:in")) if (s.when == wrap) atWrap.push_back(s);
        check("a loop wrap chases Pitch 7.25 at the loop's start, before its notes",
              atWrap.size() == 3 && same(atWrap[0].message, pitch725(61, 0x7A000000)) &&
                  same(atWrap[1].message, perNoteReset(63)) && isOn(atWrap[2], 61),
              describe(atWrap, p0));
        c.stop();
        c.setLooping(false);
        c.setSoloed(tuned, false);
        c.removeTrack(tuned);
        c.removeTrack(other);
    }

    // --- through to the touched track ------------------------------------------------------
    {
        const TrackId second = c.addTrack();
        c.setOutput(second, "Synth1:in");
        c.select(keys);
        fake.sent.clear();
        c.receive(0, on(70));
        c.tick();
        c.select(second);
        c.receive(0, off(70));
        c.receive(0, on(71));
        c.tick();
        check("a note-off goes where its note-on went; the next note to the newly touched track",
              count(fake.at("Synth2:in"), isOn, 70) == 1 && count(fake.at("Synth2:in"), isOff, 70) == 1 &&
                  count(fake.at("Synth1:in"), isOn, 71) == 1);
        c.setOutput(second, "");
        c.tick();
        check("its output going ends a live note held there", count(fake.at("Synth1:in"), isOff, 71) == 1);
        c.setMuted(keys, true);
        c.select(keys);
        fake.sent.clear();
        c.receive(0, on(72));
        c.tick();
        check("a muted track plays nothing through", fake.at("Synth2:in", true).empty());
        c.setMuted(keys, false);
        c.removeTrack(second);
        c.select(keys);
    }

    // --- a tempo change while playing --------------------------------------------------------
    c.locate(0);
    c.play();
    const uw::Nanoseconds s0 = fakeNow;
    fake.sent.clear();
    run(c, 300);
    c.setTempo(60);   // half speed from here: beat 1 comes 1 s later, not 0.5
    const uw::Nanoseconds changed = fakeNow;
    run(c, 2500);
    {
        const auto got = fake.at("Synth2:in");
        const uw::Nanoseconds beat1 = changed + 2 * (s0 + kBeat - changed);
        check("a tempo change: what's ahead moves to the new tempo, sent once, notes sounding go on",
              got.size() == 4 && count(got, isOn, 60) == 1 && isOff(got[1], 60) && got[1].when == beat1 &&
                  isOn(got[2], 62) && got[2].when == beat1,
              describe(got, s0));
    }
    c.stop();
    c.setTempo(120);

    // --- a device that comes later ----------------------------------------------------------
    {
        c.setOutput(keys, "Later:in");
        c.tick();
        check("a device that isn't there: its row says so", c.view().rows[0].outputMissing && c.view().rows[0].outputLabel == "Later:in");
        fake.destinations.push_back({"Later:in", "Later", false, true, uw::MidiProtocol::Midi2});
        ++fake.announced;
        c.tick();
        c.locate(0);
        c.play();
        fake.sent.clear();
        run(c, 200);
        check("when it comes, the track plays to it", !c.view().rows[0].outputMissing && count(fake.at("Later:in"), isOn, 60) == 1);
        c.stop();
    }

    // --- removing a track ---------------------------------------------------------------------
    c.removeTrack(keys);
    check("removing a track removes its output", c.session().outputs.empty());
    c.undo();
    check("and undo brings both back", c.session().outputOf(keys) != nullptr);

    // --- a plugin output (milestone 8) ------------------------------------------------------
    {
        const std::string pluginId   = Instruments::outputId(pluginFactory.scan()[0]);
        const TrackId      synthTrack = c.addTrack();
        c.setOutput(synthTrack, pluginId);
        c.tick();
        check("a plugin output: the row shows the plugin's name",
              c.view().rows.back().outputLabel == "Fake Synth" && !c.view().rows.back().outputMissing);

        c.setArmed(synthTrack, true);
        c.locate(0);
        c.record();
        const uw::Nanoseconds take = fakeNow;
        c.receive(0, on(60));
        c.tick();
        fakeNow = take + kBeat;
        c.receive(0, off(60));
        c.tick();
        fakeNow = take + 2 * kBeat;
        c.stop();
        c.setArmed(synthTrack, false);

        c.locate(0);
        c.play();
        const uw::Nanoseconds t0 = fakeNow;
        c.tick();   // schedules the note-on into the voice's own queue, at t0

        // Render a block straddling t0 by hand, offline-style (renderBlock(),
        // never process(): no backend is open here) -- the whole point of an
        // offline-capable engine.
        constexpr double          kSampleRate = 48000.0;
        constexpr std::uint32_t   kFrames     = 480;        // 10 ms
        constexpr uw::Nanoseconds kLead       = 5'000'000;   // 5 ms before t0
        std::vector<float>        left(kFrames, 0.0f), right(kFrames, 0.0f);
        float*                    channels[2] = {left.data(), right.data()};
        uw::AudioCallbackContext  ctx;
        ctx.frameCount      = kFrames;
        ctx.blockStartTime  = t0 - kLead;
        ctx.time.sampleRate = kSampleRate;
        ctx.output          = uw::AudioBuffer{channels, 2, kFrames};
        engine.renderBlock(ctx);

        const auto&          received = pluginFactory.lastCreated->received;
        const std::uint32_t  expected = static_cast<std::uint32_t>(kLead * kSampleRate / 1e9 + 0.5);
        check("a plugin voice gets its note at the exact sample (sample-accurate timing)",
              received.size() == 1 && ump::isNoteOn(received[0].midi) && ump::noteNumber(received[0].midi) == 60 &&
                  received[0].sampleOffset == expected);

        // The fake plugin has no editor (its hasEditor() defaults to
        // false), so opening one fails cleanly -- never a real window
        // (ctest stays silent, needs no display).
        c.openEditor(synthTrack);
        check("a plugin with no editor: opening it leaves nothing open", !c.view().rows.back().editorOpen);
        c.closeEditor(synthTrack);   // a no-op: safe when nothing is open
        c.openEditor(keys);         // keys plays to a MIDI device: no editor to open
        check("and a MIDI track has no editor at all", !c.view().rows[0].editorOpen);

        c.stop();
        c.removeTrack(synthTrack);
    }

    std::printf("%s: %d failure%s\n", failures == 0 ? "OK" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
