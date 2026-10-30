// AudioEngine (src/App/AudioEngine.h, docs/plugins.md, milestone 8) over a
// fake PluginFactory and no audio backend: opening and closing a voice,
// mixing more than one into the same block, cancelling what's pending, and
// the sample-accurate offset renderBlock() works out for an event — all
// driven by hand, offline-style, since nothing here ever calls start().
// Silent: no CLAP plugin, no audio device.

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "App/AudioEngine.h"
#include "App/Instruments.h"

namespace {

int failures = 0;

void check(const char* what, bool ok, const std::string& detail = {}) {
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str());
    if (!ok) ++failures;
}

// Fills its audio output with a DC value equal to the note number of the
// last note-on it saw (0 at first), so a test can tell which voice's plugin
// ran just from the mixed output, and records every event's sample offset.
class FakePlugin final : public uw::HostedPlugin {
public:
    struct Received {
        std::uint32_t   sampleOffset;
        uw::MidiMessage midi;
    };
    std::vector<Received> received;
    uw::PluginDescriptor   desc;
    bool                   activated = false;
    float                  level     = 0.0f;

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
    // Fills up to each event's sample with the level in force before it, so
    // a test can see the exact sample a note took effect at, not just the
    // event list.
    void process(const uw::ProcessContext& ctx) noexcept override {
        std::uint32_t cursor = 0;
        for (std::uint32_t i = 0; i < ctx.eventsIn.count; ++i) {
            const uw::Event& e = ctx.eventsIn.events[i];
            if (e.type != uw::EventType::Midi) continue;
            received.push_back({e.sampleOffset, e.midi});
            for (std::uint32_t c = 0; c < ctx.audioOut.channelCount; ++c)
                for (std::uint32_t f = cursor; f < e.sampleOffset && f < ctx.frameCount; ++f)
                    ctx.audioOut.channels[c][f] = level;
            cursor = e.sampleOffset;
            if (((e.midi.words[0] >> 20) & 0xF) == 0x9) level = static_cast<float>((e.midi.words[0] >> 8) & 0x7F);
        }
        for (std::uint32_t c = 0; c < ctx.audioOut.channelCount; ++c)
            for (std::uint32_t f = cursor; f < ctx.frameCount; ++f) ctx.audioOut.channels[c][f] = level;
    }
    void   stopProcessing() noexcept override {}
    double paramValue(std::uint32_t) const override { return 0.0; }
    void   flushParams(const uw::EventBlock&, uw::EventSink*) override {}
    bool   saveState(std::vector<std::byte>&) const override { return true; }
    bool   loadState(const std::byte*, std::size_t) override { return true; }
    void   idle() override {}
};

// Two instruments: "One" and "Two", each creating its own FakePlugin.
class FakeFactory final : public uw::PluginFactory {
public:
    std::vector<FakePlugin*> created;

    uw::PluginFormat                  format() const override { return uw::PluginFormat::Clap; }
    std::vector<uw::PluginDescriptor> scan() override {
        uw::PluginDescriptor a, b;
        a.format = b.format = uw::PluginFormat::Clap;
        a.kind = b.kind = uw::PluginKind::Instrument;
        a.path = "fake://one";
        a.id   = "one";
        a.name = "One";
        b.path = "fake://two";
        b.id   = "two";
        b.name = "Two";
        return {a, b};
    }
    std::unique_ptr<uw::HostedPlugin> create(const uw::PluginDescriptor& d, uw::HostServices&) override {
        auto plugin = std::make_unique<FakePlugin>(d);
        created.push_back(plugin.get());
        return plugin;
    }
};

uw::MidiMessage noteOn(std::uint8_t note) {
    return uw::MidiMessage{{0x40900000u | (static_cast<std::uint32_t>(note) << 8), 0x80000000u, 0, 0}, 2};
}

} // namespace

int main() {
    using namespace umpire;

    FakeFactory factory;
    AudioEngine engine(nullptr, factory);   // no backend: never started, driven by hand
    engine.instruments().rescan();

    check("the two instruments are found, sorted by name",
          engine.instruments().choices().size() == 2 && engine.instruments().choices()[0].label == "One" &&
              engine.instruments().choices()[1].label == "Two");

    check("open() with an unknown id fails", engine.open("not a plugin output") == kInvalidVoice);

    const std::string idOne = engine.instruments().choices()[0].id;
    const std::string idTwo = engine.instruments().choices()[1].id;
    check("an output id names a plugin", Instruments::isPluginOutput(idOne) && !Instruments::isPluginOutput("Synth2:in"));

    const VoiceHandle one = engine.open(idOne);
    const VoiceHandle two = engine.open(idTwo);
    check("two voices open, each its own instance",
          one != kInvalidVoice && two != kInvalidVoice && one != two && engine.connected(one) && engine.connected(two) &&
              factory.created.size() == 2 && factory.created[0] != factory.created[1]);
    check("a voice schedules, like a MIDI device that holds messages to their time", engine.schedules(one));
    bool connected = false;
    check("a voice's label is the plugin's name", engine.label(idOne, &connected) == "One" && connected);
    check("protocol: MIDI 2.0, the fake plugin's preference", engine.protocol(one) == uw::MidiProtocol::Midi2);

    constexpr double          kSampleRate = 48000.0;
    constexpr std::uint32_t   kFrames     = 480;   // 10 ms
    constexpr uw::Nanoseconds kBlockStart = 1'000'000'000;

    const auto renderOnce = [&] {
        std::vector<float>      left(kFrames, 0.0f), right(kFrames, 0.0f);
        float*                  channels[2] = {left.data(), right.data()};
        uw::AudioCallbackContext ctx;
        ctx.frameCount      = kFrames;
        ctx.blockStartTime  = kBlockStart;
        ctx.time.sampleRate = kSampleRate;
        ctx.output          = uw::AudioBuffer{channels, 2, kFrames};
        engine.renderBlock(ctx);
        return std::pair{left, right};
    };

    // Voice "one" at 3 ms into the block: sample 144. Voice "two" nothing
    // this block: silent, so its 0 doesn't disturb the mix.
    uw::MidiMessage note60 = noteOn(60);
    engine.sendAt(one, &note60, 1, kBlockStart + 3'000'000);
    auto [left1, right1] = renderOnce();
    check("sample-accurate: the note lands at its exact sample, nowhere else",
          factory.created[0]->received.size() == 1 && factory.created[0]->received[0].sampleOffset == 144 &&
              left1[143] == 0.0f && left1[144] == 60.0f && right1[144] == 60.0f);

    // Both voices sounding together: mixed (summed), not overwritten.
    uw::MidiMessage note10 = noteOn(10);
    uw::MidiMessage note20 = noteOn(20);
    engine.sendAt(one, &note10, 1, kBlockStart);
    engine.sendAt(two, &note20, 1, kBlockStart);
    auto [left2, right2] = renderOnce();
    check("two voices mix (sum), not one overwriting the other", left2[0] == 30.0f && right2[0] == 30.0f);

    // Cancelling drops what's pending (not yet due) without disturbing a
    // send made after it, even due in the very same block.
    uw::MidiMessage note5  = noteOn(5);
    uw::MidiMessage note99 = noteOn(99);
    engine.sendAt(one, &note5, 1, kBlockStart);   // would be due this block...
    engine.cancel(one);                            // ...but is dropped before it's drained
    engine.sendAt(one, &note99, 1, kBlockStart);   // sent after the cancel: keeps its place
    const std::size_t before = factory.created[0]->received.size();
    renderOnce();
    check("cancel drops what was pending, not what's sent after it",
          factory.created[0]->received.size() == before + 1 &&
              ((factory.created[0]->received.back().midi.words[0] >> 8) & 0x7F) == 99);

    // Closing one voice doesn't disturb the other (never started: close() is
    // immediate, no audio thread to wait for).
    engine.close(one);
    check("closing a voice invalidates only that one", !engine.connected(one) && engine.connected(two));
    uw::MidiMessage note7 = noteOn(7);
    engine.sendAt(two, &note7, 1, kBlockStart);
    const std::size_t before2 = factory.created[1]->received.size();
    renderOnce();
    check("the other voice still processes after its sibling closes", factory.created[1]->received.size() == before2 + 1);

    check("never started: offline all along", !engine.running());
    engine.close(two);

    std::printf("%s: %d failure%s\n", failures == 0 ? "OK" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
