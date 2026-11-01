// Milestone 9's files (docs/files.md): MIDI Clip Files (src/App/ClipFile.h)
// byte for byte, round trips, long gaps, another app's ticks per quarter and
// broken files; session bundles (src/App/SessionFile.h) saved and opened;
// and TrackController's save, open and new, with a fake plugin's state.
// Silent: a temporary folder, no MIDI hardware, no audio, no window.

#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "App/AudioEngine.h"
#include "App/ClipFile.h"
#include "App/MainLoop.h"
#include "App/MidiInputs.h"
#include "App/SessionFile.h"
#include "App/TrackController.h"
#include "uw/ump/Ump.h"

namespace {

namespace fs = std::filesystem;

int failures = 0;

void check(const char* what, bool ok, const std::string& detail = {}) {
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str());
    if (!ok) ++failures;
}

bool same(const uw::MidiMessage& a, const uw::MidiMessage& b) {
    if (a.wordCount != b.wordCount) return false;
    for (std::uint8_t i = 0; i < a.wordCount; ++i)
        if (a.words[i] != b.words[i]) return false;
    return true;
}

bool same(const std::vector<umpire::ClipEvent>& a, const std::vector<umpire::ClipEvent>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].tick != b[i].tick || !same(a[i].message, b[i].message)) return false;
    return true;
}

std::vector<std::uint32_t> wordsOf(const std::vector<std::uint8_t>& bytes) {
    std::vector<std::uint32_t> out;
    for (std::size_t i = 8; i + 4 <= bytes.size(); i += 4)
        out.push_back(std::uint32_t{bytes[i]} << 24 | std::uint32_t{bytes[i + 1]} << 16 | std::uint32_t{bytes[i + 2]} << 8 |
                      bytes[i + 3]);
    return out;
}

std::vector<std::uint8_t> fileOf(const std::vector<std::uint32_t>& words) {
    std::vector<std::uint8_t> out{'S', 'M', 'F', '2', 'C', 'L', 'I', 'P'};
    for (std::uint32_t w : words)
        for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<std::uint8_t>(w >> shift));
    return out;
}

uw::MidiMessage noteOn(std::uint8_t key) { return uw::ump::midi2NoteOn(0, 0, key, 0x8000); }
uw::MidiMessage noteOff(std::uint8_t key) { return uw::ump::midi2NoteOff(0, 0, key, 0); }

// A plugin whose state is a string: what it was given before activating, and
// what it saves.
class StatePlugin final : public uw::HostedPlugin {
public:
    uw::PluginDescriptor desc;
    std::string          state = "defaults";
    std::string          loadedBeforeActivate;   // "" if loadState came after activate, or never
    bool                 activated = false;
    int&                 alive;

    StatePlugin(uw::PluginDescriptor d, int& count) : desc(std::move(d)), alive(count) { ++alive; }
    ~StatePlugin() override { --alive; }

    const uw::PluginDescriptor& descriptor() const override { return desc; }
    uw::PortLayout              portLayout() const override {
        uw::PortLayout layout;
        layout.noteInputs.push_back(uw::NotePortInfo{"in", uw::NoteDialect::Midi2, uw::NoteDialect::Midi2});
        layout.audioOutputs.push_back(uw::AudioPortInfo{"out", 2, true});
        return layout;
    }
    std::vector<uw::ParameterInfo> parameters() const override { return {}; }
    bool   activate(double, std::uint32_t) override { return activated = true; }
    void   deactivate() override { activated = false; }
    void   process(const uw::ProcessContext&) noexcept override {}
    void   stopProcessing() noexcept override {}
    double paramValue(std::uint32_t) const override { return 0.0; }
    void   flushParams(const uw::EventBlock&, uw::EventSink*) override {}
    bool   saveState(std::vector<std::byte>& out) const override {
        for (char c : state) out.push_back(static_cast<std::byte>(c));
        return true;
    }
    bool loadState(const std::byte* data, std::size_t size) override {
        state.assign(reinterpret_cast<const char*>(data), size);
        if (!activated) loadedBeforeActivate = state;
        return true;
    }
    void idle() override {}
};

class StateFactory final : public uw::PluginFactory {
public:
    std::vector<StatePlugin*> created;   // valid while it's alive
    int                       alive = 0;

    uw::PluginFormat                  format() const override { return uw::PluginFormat::Clap; }
    std::vector<uw::PluginDescriptor> scan() override {
        uw::PluginDescriptor d;
        d.format = uw::PluginFormat::Clap;
        d.kind   = uw::PluginKind::Instrument;
        d.path   = "fake://synth";
        d.id     = "synth";
        d.name   = "Synth";
        return {d};
    }
    std::unique_ptr<uw::HostedPlugin> create(const uw::PluginDescriptor& d, uw::HostServices&) override {
        auto plugin = std::make_unique<StatePlugin>(d, alive);
        created.push_back(plugin.get());
        return plugin;
    }
};

class FakeInput final : public uw::MidiInput {
public:
    std::vector<uw::MidiSourceInfo> sources{{"KeyLab:KeyLab MIDI", "KeyLab", false, true, uw::MidiProtocol::Midi2}};
    void                            setProtocolPreference(uw::MidiProtocol) override {}
    void                            setSink(uw::MidiSink*) override {}
    std::vector<uw::MidiSourceInfo> enumerate() override { return sources; }
    uw::MidiSourceHandle            open(const std::string&) override { return 1; }
    uw::MidiSourceHandle            openVirtual(const std::string&, uw::MidiProtocol) override { return 2; }
    void                            close(uw::MidiSourceHandle) override {}
    std::uint64_t                   changes() const override { return 0; }
    const char*                     name() const override { return "fake"; }
};

} // namespace

int main() {
    using namespace umpire;
    namespace cf = umpire::clipfile;

    // Canonical, as the paths SessionFile returns are: macOS's temp folder is
    // under /var, a symlink to /private/var.
    const fs::path dir = fs::weakly_canonical(fs::temp_directory_path()) / ("umpire-file-tests-" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    // --- MIDI Clip Files ---------------------------------------------------------------
    {
        const std::vector<ClipEvent> events{{0, noteOn(60)}, {kTicksPerQuarter, noteOff(60)}};
        const auto bytes = cf::write(events, 2 * kTicksPerQuarter, cf::Tempo{120, 3});
        const auto words = wordsOf(bytes);
        const std::vector<std::uint32_t> expected{
            0x00400000,                                       // DCS 0
            0x00309D80,                                       // DCTPQ 40320
            0x00400000,                                       // DCS 0
            0xF0200000, 0, 0, 0,                              // Start of Clip
            0xD0100000, 50'000'000, 0, 0,                     // Set Tempo: 0.5 s a quarter
            0xD0100001, 0x03020800, 0, 0,                     // Set Time Signature 3/4
            noteOn(60).words[0], noteOn(60).words[1],         // at 0, sharing the DCS
            0x00409D80,                                       // DCS 40320
            noteOff(60).words[0], noteOff(60).words[1],
            0x00409D80,                                       // DCS 40320, to the end
            0xF0210000, 0, 0, 0};                             // End of Clip
        check("a clip file starts SMF2CLIP", std::memcmp(bytes.data(), "SMF2CLIP", 8) == 0);
        check("a clip file's words are the spec's: DCTPQ, Start of Clip, tempo and meter, events, End of Clip",
              words == expected);
    }
    {
        std::vector<ClipEvent> events{
            {0, uw::MidiMessage{{0x20903C64, 0, 0, 0}, 1}},                     // MIDI 1.0 note on
            {0, uw::MidiMessage{{0x40003C03, 0x12345678, 0, 0}, 2}},            // Pitch 7.25 for key 60, same tick
            {5, uw::MidiMessage{{0x30160102, 0x03040506, 0, 0}, 2}},            // SysEx7
            {7, uw::MidiMessage{{0x50000102, 0x03040506, 0x0708090A, 0x0B0C0D0E}, 4}},   // 128-bit data
            {13440, noteOff(60)}};                                               // a triplet eighth
        const auto r = cf::read(cf::write(events, 40320, {}).data(), cf::write(events, 40320, {}).size());
        check("a clip round-trips exactly: every message type's size, same-tick order, ticks",
              r.ok && same(r.events, events) && r.length == 40320 && r.ticksPerQuarter == kTicksPerQuarter &&
                  r.tempoBpm == 120,
              r.error);
    }
    {
        const Tick gap = 3 * Tick{cf::kMaxDelta} + 17;   // about 78 quarters: past one Delta Clockstamp
        const std::vector<ClipEvent> events{{1, noteOn(60)}, {1 + gap, noteOff(60)}};
        const auto bytes = cf::write(events, 2 + gap, {});
        const auto words = wordsOf(bytes);
        int nulls = 0;
        for (std::size_t i = 0; i + 1 < words.size(); ++i)
            if (words[i] == (0x00400000u | cf::kMaxDelta) && words[i + 1] == 0) ++nulls;
        const auto r = cf::read(bytes.data(), bytes.size());
        check("a gap past 20 bits is restarted with Null messages, and reads back exactly",
              nulls == 3 && r.ok && same(r.events, events) && r.length == 2 + gap);
    }
    {
        // Another app's: 480 to a quarter, a program change in the Clip
        // Configuration Header, a JR timestamp to ignore.
        const auto bytes = fileOf({0x00400000, 0x003001E0,                   // DCS 0, DCTPQ 480
                                   0x00400000, 0x20C00500,                   // header: program 5
                                   0x00400000, 0xF0200000, 0, 0, 0,          // Start of Clip
                                   0x004000F0, 0x20903C64,                   // 240: note on
                                   0x00200123,                               // JR timestamp
                                   0x004000F0, 0x20803C00,                   // 480: note off
                                   0x00400078, 0xF0210000, 0, 0, 0});        // 600: End of Clip
        const auto r = cf::read(bytes.data(), bytes.size());
        check("another DCTPQ is read in Umpire's ticks; the header's messages come first, at 0",
              r.ok && r.ticksPerQuarter == 480 && r.events.size() == 3 && r.events[0].tick == 0 &&
                  r.events[0].message.words[0] == 0x20C00500 && r.events[1].tick == 20160 && r.events[2].tick == 40320 &&
                  r.length == 50400,
              r.error);
    }
    {
        const auto noHeader = fileOf({0x00400000, 0x003001E0});
        std::vector<std::uint8_t> bad = noHeader;
        bad[0] = 'X';
        const auto noEnd = fileOf({0x00400000, 0x003001E0, 0x00400000, 0xF0200000, 0, 0, 0, 0x00400000, 0x20903C64});
        auto cut = fileOf({0x00400000, 0x003001E0, 0x00400000, 0xF0200000, 0, 0});
        const auto noTpq = fileOf({0x00400000, 0xF0200000, 0, 0, 0, 0x00400000, 0xF0210000, 0, 0, 0});
        check("broken clip files are refused, saying why",
              !cf::read(bad.data(), bad.size()).ok && !cf::read(noEnd.data(), noEnd.size()).ok &&
                  cf::read(noEnd.data(), noEnd.size()).error == "no End of Clip" && !cf::read(cut.data(), cut.size()).ok &&
                  !cf::read(noTpq.data(), noTpq.size()).ok && !cf::read(bad.data(), 3).ok);
    }

    // --- session bundles ----------------------------------------------------------------
    {
        auto clip    = std::make_shared<Clip>();
        clip->id     = 7;
        clip->name   = "Riff";
        clip->length = 4 * kTicksPerQuarter;
        clip->events = {{0, noteOn(62)}, {kTicksPerQuarter, noteOff(62)}};
        clip->scale  = Scale{31, 62};
        clip->scaleFit        = ScaleFit::Chosen;
        clip->scaleCandidates = {31};
        SavedSession s;
        s.session.tempoBpm    = 96.5;
        s.session.beatsPerBar = 3;
        s.session.looping     = true;
        s.session.loopStart   = kTicksPerQuarter;
        s.session.loopEnd     = 5 * kTicksPerQuarter;
        Track a;
        a.id     = 2;
        a.name   = "Keys";
        a.muted  = true;
        a.clips  = {ClipRef{3, clip, 0}, ClipRef{4, clip, 8 * kTicksPerQuarter}};   // one clip, two places
        Track b;
        b.id     = 5;
        b.name   = "Bass";
        b.armed  = true;
        s.session.tracks  = {a, b};
        s.session.outputs = {Output{2, "plugin:fake"}, Output{5, "Synth2:in"}};
        s.counters        = SessionCounters{9, 4, 10, 12};
        s.pluginStates[2] = PluginState{"plugin:fake", {std::byte{1}, std::byte{0}, std::byte{255}}};
        s.pluginStates[5] = PluginState{"plugin:gone", {std::byte{9}}};   // not its output any more: not saved

        const std::string bundle = (dir / "Song.umpire").string();
        const SessionFileResult saved = saveSession(bundle, s);
        check("a session saves as a bundle: session.json, one file for a clip used twice, the plugin's state",
              saved.ok && fs::is_regular_file(dir / "Song.umpire/session.json") &&
                  fs::is_regular_file(dir / "Song.umpire/clips/clip-7.midi2") &&
                  std::distance(fs::directory_iterator(dir / "Song.umpire/clips"), fs::directory_iterator()) == 1 &&
                  fs::is_regular_file(dir / "Song.umpire/plugins/track-2.state") &&
                  !fs::exists(dir / "Song.umpire/plugins/track-5.state") && !fs::exists(dir / "Song.umpire.tmp"),
              saved.error);

        SavedSession back;
        const SessionFileResult opened = loadSession(bundle, back);
        const Track* keys = back.session.track(2);
        check("it opens as it was saved: tracks, outputs, tempo, meter, loop, counters",
              opened.ok && back.session.tracks.size() == 2 && keys != nullptr && keys->name == "Keys" && keys->muted &&
                  back.session.tracks[1].armed && back.session.outputs == s.session.outputs &&
                  back.session.tempoBpm == 96.5 && back.session.beatsPerBar == 3 && back.session.looping &&
                  back.session.loopStart == kTicksPerQuarter && back.session.loopEnd == 5 * kTicksPerQuarter &&
                  back.counters == s.counters,
              opened.error);
        check("its clip comes back shared by both references, with its notes, name and scale",
              keys != nullptr && keys->clips.size() == 2 && keys->clips[0].clip == keys->clips[1].clip &&
                  keys->clips[1].start == 8 * kTicksPerQuarter && keys->clips[1].id == 4 && keys->clips[0].clip->id == 7 &&
                  same(keys->clips[0].clip->events, clip->events) && keys->clips[0].clip->length == clip->length &&
                  keys->clips[0].clip->name == "Riff" && keys->clips[0].clip->scale == clip->scale &&
                  keys->clips[0].clip->scaleFit == ScaleFit::Chosen && keys->clips[0].clip->scaleCandidates == std::vector<int>{31});
        check("the plugin's state comes back for its track's output only",
              back.pluginStates.size() == 1 && back.pluginStates[2] == s.pluginStates[2]);

        // Saving again replaces it whole: the clip gone, its file too.
        s.session.tracks[0].clips.clear();
        check("saving again replaces the bundle, dropping clips no longer used",
              saveSession(bundle, s).ok && !fs::exists(dir / "Song.umpire/clips/clip-7.midi2") &&
                  !fs::exists(dir / "Song.umpire.old"));

        std::ofstream(dir / "notes.txt") << "not a session";
        fs::create_directories(dir / "Folder");
        check("save targets: new, the session's own, another session, something else",
              classifySaveTarget((dir / "New.umpire").string(), bundle) == SaveTarget::New &&
                  classifySaveTarget(bundle + "/", bundle) == SaveTarget::Current &&
                  classifySaveTarget(bundle, "") == SaveTarget::OtherSession &&
                  classifySaveTarget((dir / "notes.txt").string(), bundle) == SaveTarget::NotASession &&
                  classifySaveTarget((dir / "Folder").string(), bundle) == SaveTarget::NotASession);
        check("paths: .umpire added, a bundle found from its session.json, the name",
              saveAsPath((dir / "Other").string()) == (dir / "Other.umpire").string() &&
                  saveAsPath(bundle) == bundle && sessionBundleOf(bundle + "/session.json") == bundle &&
                  sessionBundleOf((dir / "Folder").string()).empty() && sessionNameOf(bundle) == "Song");

        SavedSession untouched;
        untouched.session.tempoBpm = 77;
        fs::create_directories(dir / "Broken.umpire");
        std::ofstream(dir / "Broken.umpire/session.json") << R"({"format": "umpire-session", "version": 1, "tempoMap": )";
        fs::create_directories(dir / "Newer.umpire");
        std::ofstream(dir / "Newer.umpire/session.json") << R"({"format": "umpire-session", "version": 99})";
        const SessionFileResult broken = loadSession((dir / "Broken.umpire").string(), untouched);
        const SessionFileResult newer  = loadSession((dir / "Newer.umpire").string(), untouched);
        check("a broken or newer session is refused, saying why, and leaves what it was given alone",
              !broken.ok && !broken.error.empty() && !newer.ok && newer.error.find("newer Umpire") != std::string::npos &&
                  untouched.session.tempoBpm == 77 && !loadSession((dir / "Folder").string(), untouched).ok);
    }

    // --- TrackController: save, open, new ------------------------------------------------
    {
        FakeInput    fake;
        MidiInputs   inputs(&fake);
        MidiOutputs  noOutputs(nullptr);
        StateFactory factory;
        MainLoop     loop;
        AudioEngine  engine(nullptr, factory, loop);
        engine.instruments().rescan();
        const std::string synth = engine.instruments().choices().at(0).id;
        uw::Nanoseconds clock = 1'000'000'000;
        TrackController c(inputs, noOutputs, engine, [&clock] { return clock; });
        c.tick();
        check("a new session is untitled and unmodified", c.view().sessionName == "Untitled" && !c.view().modified && c.path().empty());

        const TrackId t = c.addTrack();
        c.setOutput(t, synth);
        c.setTempo(100);
        c.setArmed(t, true);
        c.record();
        c.receive(1, noteOn(64));
        clock += 250'000'000;
        c.tick();
        c.receive(1, noteOff(64));
        clock += 250'000'000;
        c.tick();
        c.stop();
        check("recording made a clip, and the plugin opened", c.tracks().at(0).clips.size() == 1 && factory.created.size() == 1,
              std::to_string(factory.created.size()));
        check("changes make it modified", c.view().modified);
        factory.created.at(0)->state = "warm pad";   // turned in its editor, say

        const std::string bundle = (dir / "Take.umpire").string();
        const SessionFileResult saved = c.save(bundle);
        check("saving names it after the bundle and clears modified",
              saved.ok && c.path() == bundle && c.view().sessionName == "Take" && !c.view().modified, saved.error);
        c.setMuted(t, true);
        check("a change after saving is unsaved", c.view().modified);
        c.undo();
        check("undoing back to what was saved isn't", !c.view().modified);
        c.setMuted(t, true);

        TrackController d(inputs, noOutputs, engine, [&clock] { return clock; });
        const SessionFileResult opened = d.open(bundle);
        d.tick();   // the outputs open
        const Track& back = d.tracks().at(0);
        check("another controller opens it as saved: the track, its clip's notes, the tempo, nothing to undo",
              opened.ok && d.tracks().size() == 1 && back.id == t && back.armed && !back.muted && back.clips.size() == 1 &&
                  same(back.clips[0].clip->events, c.tracks()[0].clips[0].clip->events) && d.session().tempoBpm == 100 &&
                  d.view().undoLabel.empty() && !d.view().modified && d.view().sessionName == "Take",
              opened.error);
        check("its plugin opens with the saved state, given before it's activated",
              factory.created.size() == 2 && factory.created[1]->loadedBeforeActivate == "warm pad");
        const TrackId next = d.addTrack();
        check("ids go on from the saved counters, never reused", next > t && d.view().rows.back().name == "Track 2");

        const SessionFileResult missing = d.open((dir / "Missing.umpire").string());
        check("a failed open leaves the session as it was", !missing.ok && d.tracks().size() == 2 && d.path() == bundle);

        d.newSession();
        d.tick();
        check("New: empty, untitled, nothing to undo, its plugin closed",
              d.tracks().empty() && d.path().empty() && d.view().sessionName == "Untitled" && !d.view().modified &&
                  d.view().undoLabel.empty() && factory.alive == 1);
    }

    fs::remove_all(dir);
    std::printf("%s\n", failures == 0 ? "all passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
