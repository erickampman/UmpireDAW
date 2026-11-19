// Milestone P1, the plugin spike (docs/plugin-version.md): the built
// Umpire.clap, loaded through uw-core's CLAP host as a host would load it.
// Its descriptor and ports, a few blocks, and its state: a saved clip goes
// in, comes back out byte for byte, and a state that isn't Umpire's is
// refused. Its setup file (UMPIRE_PLUGIN_CONF, set by CTest) says no synth
// and no MIDI input, so it's silent and needs neither.

#include <cstdio>
#include <string>
#include <vector>

#include "App/ClipFile.h"
#include "Plugin/PluginSetup.h"
#include "uw/clap/ClapPluginFactory.h"
#include "uw/plugin/HostServices.h"
#include "uw/ump/Ump.h"

namespace {

using namespace umpire;
namespace ump = uw::ump;

int failures = 0;

void check(const char* what, bool ok, const std::string& detail = {}) {
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str());
    if (!ok) ++failures;
}

class TestHost final : public uw::HostServices {
public:
    void requestRestart() override {}
    void requestProcess() override {}
    void requestParamFlush() override {}
    void log(uw::LogLevel, const char* message) override { std::printf("  (plugin: %s)\n", message); }
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: test-plugin-clap <Umpire.clap>\n");
        return 2;
    }
    uw::ClapPluginFactory                   factory;
    const std::vector<uw::PluginDescriptor> found = factory.scanFile(argv[1]);
    check("one plugin in the .clap", found.size() == 1);
    if (found.size() != 1) return 1;
    check("it's Umpire, an instrument", found[0].id == "com.unlikelyware.umpire" && found[0].name == "Umpire" &&
                                            found[0].kind == uw::PluginKind::Instrument);

    TestHost host;
    auto     plugin = factory.create(found[0], host);
    check("it's created", plugin != nullptr);
    if (!plugin) return 1;

    const uw::PortLayout layout = plugin->portLayout();
    check("stereo out, nothing in", layout.audioOutputs.size() == 1 && layout.audioOutputs[0].channelCount == 2 &&
                                        layout.audioInputs.empty());
    check("no notes from the host", layout.noteInputs.empty());
    check("a window of its own, even without a synth (P3)", plugin->hasEditor());

    check("it activates", plugin->activate(48000, 512));
    std::vector<float> left(512, 1.0f), right(512, 1.0f);
    float*             channels[2] = {left.data(), right.data()};
    bool               silent      = true;
    for (int i = 0; i < 20; ++i) {
        uw::ProcessContext ctx;
        ctx.frameCount        = 512;
        ctx.time.sampleRate   = 48000;
        ctx.time.playing      = true;
        ctx.time.beatPosition = i * 512.0 / 48000.0 * 2.0;
        ctx.audioOut          = uw::AudioBuffer{channels, 2, 512};
        plugin->process(ctx);
        for (int f = 0; f < 512; ++f) silent = silent && left[f] == 0.0f && right[f] == 0.0f;
        plugin->idle();
    }
    check("with no synth, it writes silence", silent);

    std::vector<std::byte> saved;
    check("it saves", plugin->saveState(saved));
    plugin::SavedState state;
    std::string        error;
    check("its state is Umpire's, empty",
          plugin::decodeState(reinterpret_cast<const std::uint8_t*>(saved.data()), saved.size(), state, error) &&
              state.takes.empty() && state.synthId.empty(), error);

    // Two takes go in through the state and come out the same: one from the
    // song's start, one from beat 10.
    std::vector<ClipEvent> events = {
        {0, ump::midi2NoteOn(0, 0, 60, 0x8000, 60 << 9 | 100)},
        {kTicksPerQuarter / 3, ump::midi2NoteOff(0, 0, 60)},
        {5 * kTicksPerQuarter, ump::midi2NoteOn(0, 1, 67, 0x4000)},
        {6 * kTicksPerQuarter, ump::midi2NoteOff(0, 1, 67)},
    };
    plugin::SavedState in;
    in.takes             = {plugin::savedTake(0, clipfile::write(events, 8 * kTicksPerQuarter, {120, 4})),
                            plugin::savedTake(10 * kTicksPerQuarter, clipfile::write(events, 7 * kTicksPerQuarter, {120, 4}))};
    const auto  inBytes  = plugin::encodeState(in);
    const auto* inData   = reinterpret_cast<const std::byte*>(inBytes.data());
    check("it loads two takes", plugin->loadState(inData, inBytes.size()));
    saved.clear();
    plugin->saveState(saved);
    check("and saves it again, the same",
          plugin::decodeState(reinterpret_cast<const std::uint8_t*>(saved.data()), saved.size(), state, error) &&
              state.takes.size() == 2 && state.takes[0].start == 0 && state.takes[0].clipFile == in.takes[0].clipFile &&
              state.takes[1].start == in.takes[1].start && state.takes[1].clipFile == in.takes[1].clipFile);

    // P4: names and scales come back; a synth that isn't installed is kept
    // as it was; so is an input that isn't here.
    in.takes[0].name     = "Verse";
    in.takes[0].scale    = Scale{31, 57};
    in.takes[0].scaleFit = ScaleFit::Chosen;
    in.nextTakeNumber    = 9;
    in.synthChosen       = true;
    in.synthId           = "org.example.not-installed";
    in.synthPath         = "/nowhere/NotInstalled.clap";
    in.synthName         = "NotInstalled";
    in.synthState        = {std::byte{1}, std::byte{2}};
    in.input             = plugin::InputChoice{plugin::InputMode::Named, "No Such Input", "no-such-input"};
    const auto named     = plugin::encodeState(in);
    check("a version 4 state loads", plugin->loadState(reinterpret_cast<const std::byte*>(named.data()), named.size()));
    saved.clear();
    plugin->saveState(saved);
    check("names, scales and the next number come back",
          plugin::decodeState(reinterpret_cast<const std::uint8_t*>(saved.data()), saved.size(), state, error) &&
              state.takes.size() == 2 && state.takes[0].name == "Verse" && state.takes[0].scale == in.takes[0].scale &&
              state.takes[0].scaleFit == ScaleFit::Chosen && state.takes[1].name == "Take 9" &&
              state.takes[1].scaleFit != ScaleFit::Chosen && state.nextTakeNumber == 10,
          error);
    check("a synth that isn't installed is kept, state and all",
          state.synthChosen && state.synthId == in.synthId && state.synthPath == in.synthPath &&
              state.synthName == in.synthName && state.synthState == in.synthState);
    check("and so is an input that isn't here", state.input == in.input);

    const std::byte junk[] = {std::byte{'R'}, std::byte{'I'}, std::byte{'F'}, std::byte{'F'}, std::byte{0}, std::byte{0},
                              std::byte{0},   std::byte{0}};
    check("another plugin's state is refused", !plugin->loadState(junk, sizeof junk));

    plugin->stopProcessing();
    plugin->deactivate();
    plugin.reset();
    std::printf("%s\n", failures == 0 ? "all passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
