// Milestone P1, the plugin spike (docs/plugin-version.md): the built
// Umpire.clap hosting the real synth (PlainOldSynth), driven over the CLAP C
// API as a host would drive it, rendering into buffers that go nowhere, so
// silent. A clip loaded through the state plays back from the host's
// position: the synth sounds; Pitch 7.25 reaches it whole (the pitch it
// sounds, measured); a note starts when its beat comes, at 120 bpm
// and at 90; two instances at once share the synth's module; an offline render (the render extension) gives the same
// samples as a real-time one; and saving brings the synth's state back. P4:
// the synth chosen in the state (none, PlainOldSynth swapped in while it
// runs, one that isn't installed kept as it was).
//
// Skipped (77) if PlainOldSynth isn't installed. Its setup file
// (UMPIRE_PLUGIN_CONF, set by CTest) names the synth and no MIDI input.

#include <clap/clap.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "App/ClipFile.h"
#include "Plugin/PluginSetup.h"
#include "uw/clap/ClapModule.h"
#include "uw/ump/Ump.h"

namespace {

using namespace umpire;
namespace ump = uw::ump;

int failures = 0;

void check(const char* what, bool ok, const std::string& detail = {}) {
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str());
    if (!ok) ++failures;
}

// The host: no extensions, a main-thread callback when asked.
bool callbackWanted = false;
const void* hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void hostRequestRestart(const clap_host_t*) {}
void hostRequestProcess(const clap_host_t*) {}
void hostRequestCallback(const clap_host_t*) { callbackWanted = true; }
const clap_host_t kHost = {CLAP_VERSION_INIT, nullptr, "umpire-render-test", "Unlikelyware", "", "0",
                           hostGetExtension, hostRequestRestart, hostRequestProcess, hostRequestCallback};

struct Reader {
    const std::vector<std::uint8_t>* bytes;
    std::size_t                      at = 0;
};
std::int64_t readStream(const clap_istream_t* s, void* buffer, std::uint64_t size) {
    auto*      r = static_cast<Reader*>(s->ctx);
    const auto n = std::min<std::uint64_t>(size, r->bytes->size() - r->at);
    std::memcpy(buffer, r->bytes->data() + r->at, n);
    r->at += n;
    return static_cast<std::int64_t>(n);
}
std::int64_t writeStream(const clap_ostream_t* s, const void* buffer, std::uint64_t size) {
    auto* out = static_cast<std::vector<std::uint8_t>*>(s->ctx);
    out->insert(out->end(), static_cast<const std::uint8_t*>(buffer), static_cast<const std::uint8_t*>(buffer) + size);
    return static_cast<std::int64_t>(size);
}

constexpr double        kRate   = 48000;
constexpr std::uint32_t kFrames = 512;

class Instance {
public:
    Instance(const clap_plugin_factory_t* factory) {
        plugin = factory->create_plugin(factory, &kHost, "com.unlikelyware.umpire");
        if (plugin != nullptr && !plugin->init(plugin)) {
            plugin->destroy(plugin);
            plugin = nullptr;
        }
        if (plugin == nullptr) return;
        state  = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
        render = static_cast<const clap_plugin_render_t*>(plugin->get_extension(plugin, CLAP_EXT_RENDER));
    }
    ~Instance() {
        if (plugin == nullptr) return;
        if (processing) plugin->stop_processing(plugin);
        if (active) plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }

    bool load(const std::vector<std::uint8_t>& bytes) {
        Reader          r{&bytes};
        clap_istream_t  s{&r, readStream};
        return state->load(plugin, &s);
    }
    std::vector<std::uint8_t> save() {
        std::vector<std::uint8_t> out;
        clap_ostream_t            s{&out, writeStream};
        state->save(plugin, &s);
        return out;
    }
    void start() {
        active     = plugin->activate(plugin, kRate, 1, kFrames);
        processing = active && plugin->start_processing(plugin);
    }

    // `seconds` of the song from `startBeat` at `tempo`, left channel.
    std::vector<float> play(double tempo, double seconds, double startBeat = 0) {
        std::vector<float> out;
        std::vector<float> left(kFrames), right(kFrames);
        float*             channels[2] = {left.data(), right.data()};
        clap_audio_buffer_t buffer{};
        buffer.data32        = channels;
        buffer.channel_count = 2;
        clap_input_events_t  in{nullptr, [](const clap_input_events_t*) -> std::uint32_t { return 0; },
                               [](const clap_input_events_t*, std::uint32_t) -> const clap_event_header_t* { return nullptr; }};
        clap_output_events_t outEvents{nullptr, [](const clap_output_events_t*, const clap_event_header_t*) { return true; }};
        const auto blocks = static_cast<int>(seconds * kRate / kFrames);
        for (int i = 0; i < blocks; ++i) {
            clap_event_transport_t t{};
            t.header.size     = sizeof t;
            t.header.type     = CLAP_EVENT_TRANSPORT;
            t.flags           = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING |
                      CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
            t.tempo           = tempo;
            t.tsig_num        = 4;
            t.tsig_denom      = 4;
            const double beat = startBeat + i * static_cast<double>(kFrames) / kRate * tempo / 60.0;
            t.song_pos_beats  = static_cast<clap_beattime>(std::llround(beat * CLAP_BEATTIME_FACTOR));
            clap_process_t p{};
            p.steady_time         = static_cast<std::int64_t>(i) * kFrames;
            p.frames_count        = kFrames;
            p.transport           = &t;
            p.audio_outputs       = &buffer;
            p.audio_outputs_count = 1;
            p.in_events           = &in;
            p.out_events          = &outEvents;
            plugin->process(plugin, &p);
            out.insert(out.end(), left.begin(), left.end());
            if (callbackWanted) {   // the main thread's turn, as a host gives it
                callbackWanted = false;
                plugin->on_main_thread(plugin);
            }
        }
        return out;
    }

    const clap_plugin_t*        plugin = nullptr;
    const clap_plugin_state_t*  state  = nullptr;
    const clap_plugin_render_t* render = nullptr;
    bool                        active = false, processing = false;
};

// The first frame louder than -60 dB, or -1.
long firstSound(const std::vector<float>& audio, std::size_t from = 0) {
    for (std::size_t i = from; i < audio.size(); ++i)
        if (std::fabs(audio[i]) > 0.001f) return static_cast<long>(i);
    return -1;
}

// The fundamental of `audio` over [from, from + n), by autocorrelation: the
// shortest period whose correlation is near the best (no octave errors).
double pitchHz(const std::vector<float>& audio, std::size_t from, std::size_t n) {
    std::vector<double> r(700, 0.0);
    double              best = 0;
    for (std::size_t lag = 40; lag < r.size(); ++lag) {
        double sum = 0, a = 0, b = 0;
        for (std::size_t i = from; i < from + n; ++i) {
            sum += audio[i] * audio[i + lag];
            a += audio[i] * audio[i];
            b += audio[i + lag] * audio[i + lag];
        }
        r[lag] = a > 0 && b > 0 ? sum / std::sqrt(a * b) : 0;
        best   = std::max(best, r[lag]);
    }
    for (std::size_t lag = 41; lag + 1 < r.size(); ++lag) {
        if (r[lag] < 0.9 * best || r[lag] < r[lag - 1] || r[lag] < r[lag + 1]) continue;
        const double d = (r[lag - 1] - r[lag + 1]) / (2 * (r[lag - 1] - 2 * r[lag] + r[lag + 1]));   // parabolic
        return kRate / (static_cast<double>(lag) + d);
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: test-plugin-render <Umpire.clap>\n");
        return 2;
    }
    const std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
#ifdef __APPLE__
    const std::string synthPath = home + "/Library/Audio/Plug-Ins/CLAP/PlainOldSynth.clap";
#else
    const std::string synthPath = home + "/.clap/PlainOldSynth.clap";
#endif
    if (std::FILE* f = std::fopen(synthPath.c_str(), "r")) {
        std::fclose(f);
    } else {
        std::printf("SKIP  PlainOldSynth isn't installed (%s)\n", synthPath.c_str());
        return 77;
    }

    auto module = uw::ClapModule::load(argv[1]);
    check("Umpire.clap loads", module != nullptr);
    if (!module) return 1;

    // A clip: key 60 on beat 1 as it is; key 64 on beat 3, then a Pitch 7.25
    // a quarter-tone up (after its note-on, as KeyboardEDO's glide sends it);
    // key 67 on beat 5 with a Pitch 7.9 attribute a quarter-tone up, as
    // KeyboardEDO sends.
    uw::MidiMessage tune;
    tune.words[0]  = 0x40000000u | 64u << 8 | 3;   // registered per-note controller 3, key 64
    tune.words[1]  = (64u << 25) + (1u << 24);      // 64.5 in 7.25
    tune.wordCount = 2;
    const std::vector<ClipEvent> events = {
        {1 * kTicksPerQuarter, ump::midi2NoteOn(0, 0, 60, 0xC000)},
        {2 * kTicksPerQuarter, ump::midi2NoteOff(0, 0, 60)},
        {3 * kTicksPerQuarter, ump::midi2NoteOn(0, 0, 64, 0xC000)},
        {3 * kTicksPerQuarter, tune},
        {4 * kTicksPerQuarter, ump::midi2NoteOff(0, 0, 64)},
        {5 * kTicksPerQuarter, ump::midi2NoteOn(0, 0, 67, 0xC000, 67 << 9 | 256)},   // 67.5 in 7.9
        {5 * kTicksPerQuarter + kTicksPerQuarter * 4 / 5, ump::midi2NoteOff(0, 0, 67)},
    };
    plugin::SavedState saved;
    saved.takes       = {plugin::savedTake(0, clipfile::write(events, 6 * kTicksPerQuarter, {120, 4}))};
    const auto state  = plugin::encodeState(saved);

    std::vector<float> realtime, offline, slower;
    std::vector<std::uint8_t> savedAgain;
    {
        Instance a(module->factory());
        check("it hosts PlainOldSynth", a.plugin != nullptr && a.state != nullptr && a.render != nullptr);
        if (a.plugin == nullptr) return 1;
        check("the clip loads through the state", a.load(state));
        a.start();
        check("it activates, with the synth", a.processing);
        realtime   = a.play(120, 3.0);
        savedAgain = a.save();
    }
    {
        Instance b(module->factory());
        b.load(state);
        check("the render extension takes offline", b.render->set(b.plugin, CLAP_RENDER_OFFLINE));
        b.start();
        offline = b.play(120, 3.0);
    }
    {
        Instance c(module->factory());
        c.load(state);
        c.start();
        slower = c.play(90, 3.0);
    }

    // Two instances at once, each with its own synth from the one module; the
    // first goes, and the second still plays (check 6, as far as no device goes).
    std::vector<float> second;
    {
        auto d = std::make_unique<Instance>(module->factory());
        Instance e(module->factory());
        d->load(state);
        e.load(state);
        d->start();
        e.start();
        d->play(120, 0.5);
        d.reset();
        second = e.play(120, 3.0);
    }
    check("a second instance plays on when the first goes", second == realtime);

    // The chase (playback.md): key 62 tuned a quarter-tone up at beat 0.5,
    // with no note sounding (a tuning table), and its note on beat 2. Played
    // from beat 1, past the tuning, the chase sends it again before anything
    // plays; PlainOldSynth keeps Pitch 7.25 for the key (since 2026-11-19),
    // so the note sounds at 62.5, 302.3 Hz, not 293.7.
    std::vector<float> chased;
    {
        uw::MidiMessage tuneKey;
        tuneKey.words[0]  = 0x40000000u | 62u << 8 | 3;
        tuneKey.words[1]  = (62u << 25) + (1u << 24);   // 62.5 in 7.25
        tuneKey.wordCount = 2;
        const std::vector<ClipEvent> tuning = {
            {kTicksPerQuarter / 2, tuneKey},
            {2 * kTicksPerQuarter, ump::midi2NoteOn(0, 0, 62, 0xC000)},
            {3 * kTicksPerQuarter, ump::midi2NoteOff(0, 0, 62)},
        };
        plugin::SavedState table;
        table.takes = {plugin::savedTake(0, clipfile::write(tuning, 4 * kTicksPerQuarter, {120, 4}))};
        Instance f(module->factory());
        f.load(plugin::encodeState(table));
        f.start();
        chased = f.play(120, 1.5, 1.0);
    }
    const double chasedHz = pitchHz(chased, 30000, 4096);   // beat 2 is frame 24000
    check("played from past a key's Pitch 7.25, the chase tunes its note (302.3 Hz)",
          std::fabs(chasedHz / 302.27 - 1) < 0.005, std::to_string(chasedHz));

    const long first = firstSound(realtime);
    check("the synth plays the clip", first >= 0);
    // Beat 1 at 120 bpm: 0.5 s, frame 24000. The synth's own attack may be a few frames.
    check("its first note starts on beat 1", first >= 24000 && first < 24000 + 64, std::to_string(first));
    check("nothing before", firstSound(std::vector<float>(realtime.begin(), realtime.begin() + 24000)) < 0);
    const long firstSlow = firstSound(slower);
    check("at 90 bpm, beat 1 is later (frame 32000)", firstSlow >= 32000 && firstSlow < 32000 + 64, std::to_string(firstSlow));

    // Per-note pitch reaches the synth whole: tuned a quarter-tone up, key 64
    // sounds at 339.3 Hz, not 329.6, and key 67 at 403.5, not 392.0.
    const double c4 = pitchHz(realtime, 30000, 4096), tuned = pitchHz(realtime, 78000, 4096),
                 attr = pitchHz(realtime, 124800, 4096);
    check("key 60 sounds at 261.6 Hz", std::fabs(c4 / 261.63 - 1) < 0.005, std::to_string(c4));
    check("key 64 with Pitch 7.25 at 64.5 sounds at 339.3 Hz", std::fabs(tuned / 339.29 - 1) < 0.005, std::to_string(tuned));
    check("key 67 with Pitch 7.9 at 67.5 sounds at 403.5 Hz", std::fabs(attr / 403.48 - 1) < 0.005, std::to_string(attr));

    std::size_t differ = 0;
    double      peak   = 0;
    for (std::size_t i = 0; i < std::min(realtime.size(), offline.size()); ++i) {
        if (realtime[i] != offline[i]) ++differ;
        peak = std::max(peak, static_cast<double>(std::fabs(realtime[i] - offline[i])));
    }
    check("an offline render is the same as real time", realtime.size() == offline.size() && differ == 0,
          std::to_string(differ) + " samples differ, by up to " + std::to_string(peak));

    plugin::SavedState back;
    std::string        error;
    check("its state keeps the take and the synth's",
          plugin::decodeState(savedAgain.data(), savedAgain.size(), back, error) && back.takes.size() == 1 &&
              back.takes[0].clipFile == saved.takes[0].clipFile &&
              back.synthId == "com.unlikelyware.plainoldsynth" && !back.synthState.empty(),
          error);
    // The arm, through CLAP: one parameter, Record, on in a new instance,
    // turned off by the host and kept off in the state (a new instance's
    // default doesn't override it).
    {
        Instance g(module->factory());
        const auto* params = static_cast<const clap_plugin_params_t*>(g.plugin->get_extension(g.plugin, CLAP_EXT_PARAMS));
        clap_param_info_t info{};
        double            value = -1;
        check("one parameter, Record, stepped, on by default",
              params != nullptr && params->count(g.plugin) == 1 && params->get_info(g.plugin, 0, &info) &&
                  std::string(info.name) == "Record" && (info.flags & CLAP_PARAM_IS_STEPPED) != 0 &&
                  info.default_value == 1 && params->get_value(g.plugin, info.id, &value) && value == 1);
        clap_event_param_value_t armEvent{};
        armEvent.header.size  = sizeof armEvent;
        armEvent.header.type  = CLAP_EVENT_PARAM_VALUE;
        armEvent.param_id     = info.id;
        armEvent.note_id      = -1;
        armEvent.port_index   = -1;
        armEvent.channel      = -1;
        armEvent.key          = -1;
        armEvent.value        = 0;
        clap_input_events_t in{&armEvent, [](const clap_input_events_t*) -> std::uint32_t { return 1; },
                               [](const clap_input_events_t* list, std::uint32_t) {
                                   return static_cast<const clap_event_header_t*>(list->ctx);
                               }};
        clap_output_events_t out{nullptr, [](const clap_output_events_t*, const clap_event_header_t*) { return true; }};
        params->flush(g.plugin, &in, &out);
        check("flush disarms it", params->get_value(g.plugin, info.id, &value) && value == 0);
        plugin::SavedState disarmedState;
        const auto         bytes = g.save();
        check("the state keeps it off",
              plugin::decodeState(bytes.data(), bytes.size(), disarmedState, error) && !disarmedState.armed, error);
        Instance h(module->factory());
        h.load(bytes);
        check("and loads it off", h.plugin != nullptr && params->get_value(h.plugin, info.id, &value) && value == 0);
    }

    // P4: the synth is the instance's, chosen through the state (the window
    // chooses it the same way): none is silent; PlainOldSynth chosen while
    // the plugin runs is swapped in by the audio thread and sounds.
    {
        const auto peakOf = [](const std::vector<float>& v) {
            float p = 0;
            for (const float x : v) p = std::max(p, std::fabs(x));
            return p;
        };
        Instance           g(module->factory());
        plugin::SavedState none = saved;
        none.synthChosen        = true;
        check("a state choosing no synth loads", g.load(plugin::encodeState(none)));
        g.start();
        check("and it's silent", peakOf(g.play(120, 3.0)) == 0.0f);
        auto bytes = g.save();
        check("it saves none chosen", plugin::decodeState(bytes.data(), bytes.size(), back, error) && back.synthChosen &&
                                          back.synthId.empty(), error);
        plugin::SavedState pos = saved;
        pos.synthChosen        = true;
        pos.synthId            = "com.unlikelyware.plainoldsynth";
        check("a state choosing PlainOldSynth loads while it runs", g.load(plugin::encodeState(pos)));
        check("PlainOldSynth is swapped in and sounds", peakOf(g.play(120, 3.0)) > 0.01f);
        bytes = g.save();
        check("it saves PlainOldSynth chosen, with its file and name",
              plugin::decodeState(bytes.data(), bytes.size(), back, error) && back.synthChosen &&
                  back.synthId == "com.unlikelyware.plainoldsynth" && back.synthName == "PlainOldSynth" &&
                  back.synthPath.find("PlainOldSynth") != std::string::npos && !back.synthState.empty(),
              error);
        // One that isn't installed: silence, and kept as it was.
        plugin::SavedState gone = saved;
        gone.synthChosen        = true;
        gone.synthId            = "org.example.not-installed";
        gone.synthPath          = "/nowhere/NotInstalled.clap";
        gone.synthName          = "NotInstalled";
        gone.synthState         = {std::byte{7}, std::byte{8}, std::byte{9}};
        check("a state choosing a synth that isn't here loads", g.load(plugin::encodeState(gone)));
        check("and it's silent", peakOf(g.play(120, 3.0)) == 0.0f);
        bytes = g.save();
        check("the synth that isn't here is kept, state and all",
              plugin::decodeState(bytes.data(), bytes.size(), back, error) && back.synthChosen &&
                  back.synthId == gone.synthId && back.synthPath == gone.synthPath && back.synthName == "NotInstalled" &&
                  back.synthState == gone.synthState,
              error);
    }

    std::printf("%s\n", failures == 0 ? "all passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
