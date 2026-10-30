// The track view's controller (src/App/TrackController.h) over a fake MIDI
// input and a fake clock: the inputs, tracks and their commands, undo, the
// transport, recording takes into clips, moving and deleting clips, the
// activity lights and the monitor. Silent, no window, no MIDI hardware.

#include <cmath>
#include <cstdio>
#include <string>

#include "App/AudioEngine.h"
#include "App/GridModel.h"
#include "App/MainLoop.h"
#include "App/MidiInputs.h"
#include "App/TrackController.h"
#include "uw/ump/Ump.h"

namespace {

int failures = 0;

void check(const char* what, bool ok, const std::string& detail = {}) {
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str());
    if (!ok) ++failures;
}

// No CLAP plugins on this machine: AudioEngine has its own tests
// (AudioEngineTests.cpp), and playback has its own (PlaybackTests.cpp).
class NoPlugins final : public uw::PluginFactory {
public:
    uw::PluginFormat                  format() const override { return uw::PluginFormat::Clap; }
    std::vector<uw::PluginDescriptor> scan() override { return {}; }
    std::unique_ptr<uw::HostedPlugin> create(const uw::PluginDescriptor&, uw::HostServices&) override { return nullptr; }
};

// Two hardware devices and an app's source; handles count from 1 as they're opened.
class FakeInput final : public uw::MidiInput {
public:
    uw::MidiSink*                   sink = nullptr;
    std::vector<uw::MidiSourceInfo> sources{{"KeyLab:KeyLab MIDI", "KeyLab", false, true, uw::MidiProtocol::Midi1},
                                            {"Launchkey:Launchkey MIDI", "Launchkey", false, true, uw::MidiProtocol::Midi1},
                                            {"KeyEDO", "KeyEDO", false, false, uw::MidiProtocol::Midi2}};
    std::map<std::string, uw::MidiSourceHandle> opened;   // by id or virtual name
    uw::MidiSourceHandle                        next = 1;
    std::uint64_t                               announced = 0;

    void setProtocolPreference(uw::MidiProtocol) override {}
    void setSink(uw::MidiSink* s) override { sink = s; }
    std::vector<uw::MidiSourceInfo> enumerate() override { return sources; }
    uw::MidiSourceHandle open(const std::string& id) override {
        for (const auto& s : sources)
            if (s.id == id) return opened[id] = next++;
        return uw::kInvalidMidiSource;
    }
    uw::MidiSourceHandle openVirtual(const std::string& name, uw::MidiProtocol) override { return opened[name] = next++; }
    void                 close(uw::MidiSourceHandle) override {}
    std::uint64_t        changes() const override { return announced; }
    const char*          name() const override { return "fake"; }
};

uw::MidiMessage noteOn(std::uint8_t group, std::uint8_t channel, std::uint8_t key = 60) {
    return uw::ump::midi2NoteOn(group, channel, key, 0x8000);
}

// MIDI 2.0 Registered Per-Note Controller 3, Pitch 7.25: a key's tuning, with no note.
uw::MidiMessage pitch725(std::uint8_t key, std::uint32_t value) {
    return uw::MidiMessage{{0x40000000u | (std::uint32_t{key} << 8) | 3u, value, 0, 0}, 2};
}

// MIDI 2.0 Per-Note Management, its reset flag (S) set.
uw::MidiMessage perNoteReset(std::uint8_t key) {
    return uw::MidiMessage{{0x40F00000u | (std::uint32_t{key} << 8) | 0x01u, 0, 0, 0}, 2};
}

constexpr uw::Nanoseconds kBeat = 500'000'000;   // at 120

} // namespace

int main() {
    using namespace umpire;
    namespace ump = uw::ump;

    {
        std::vector<ClipEvent> events{{0, uw::MidiMessage{{0x20903C64, 0, 0, 0}, 1}},
                                      {10, uw::MidiMessage{{0x20903C00, 0, 0, 0}, 1}},   // velocity 0: off
                                      {20, noteOn(0, 0, 62)}};
        const auto notes = notesOf(events, 50);
        check("notesOf pairs a MIDI 1.0 note-on with its velocity-0 off, and ends a held note at the end",
              notes.size() == 2 && notes[0].length == 10 && notes[1].note == 62 && notes[1].length == 30);
    }

    FakeInput  fake;
    MidiInputs inputs(&fake);
    inputs.openAll();
    check("the virtual inputs and the hardware are opened, not the app's source",
          fake.opened.count(MidiInputs::kVirtualMidi2) == 1 && fake.opened.count("KeyLab:KeyLab MIDI") == 1 &&
              fake.opened.count("KeyEDO") == 0);
    check("listening: the hardware, then the virtual inputs",
          inputs.listening() == std::vector<std::string>{"KeyLab", "Launchkey", "MIDI 2.0 In", "MIDI 1.0 In"});

    uw::Nanoseconds clock = 1'000'000'000;
    MidiOutputs     noOutputs(nullptr);   // playback has its own tests (PlaybackTests.cpp)
    NoPlugins       noPlugins;
    MainLoop        noLoop;
    AudioEngine     noEngine(nullptr, noPlugins, noLoop);
    TrackController c(inputs, noOutputs, noEngine, [&clock] { return clock; });
    const uw::MidiSourceHandle keylab    = fake.opened["KeyLab:KeyLab MIDI"];
    const uw::MidiSourceHandle launchkey = fake.opened["Launchkey:Launchkey MIDI"];
    c.tick();
    check("a new session is empty and says to play something",
          c.view().rows.empty() && c.view().status == "Play something to start" &&
              c.view().listening == "KeyLab, Launchkey, MIDI 2.0 In, MIDI 1.0 In" && c.view().undoLabel.empty());

    // From the MIDI thread, as the platform input delivers.
    inputs.onMidi(launchkey, uw::TimedMidiMessage{clock, noteOn(0, 0)});
    c.tick();
    check("the first note makes a track, touched and lit",
          c.view().rows.size() == 1 && c.view().rows[0].name == "Track 1" && c.view().rows[0].touched &&
              c.view().rows[0].midiActive && c.view().status == "Playing goes to Track 1");
    check("the monitor shows it, its source and the track",
          c.view().monitor.size() == 1 && c.view().monitor[0].source == "Launchkey" &&
              c.view().monitor[0].tracks == "Track 1" && c.view().monitor[0].text.find("NoteOn") != std::string::npos);
    clock += 200'000'000;
    c.tick();
    check("the activity light goes out", !c.view().rows[0].midiActive);

    const TrackId t1 = c.tracks()[0].id;
    const TrackId t2 = c.addTrack();
    check("adding a track puts it after the touched one, and touches it",
          c.tracks().size() == 2 && c.tracks()[1].id == t2 && c.selected() == t2 && c.view().rows[1].name == "Track 2");
    c.receive(keylab, noteOn(3, 9));
    c.tick();
    check("playing goes to the touched track only, from any input, group or channel",
          !c.view().rows[0].midiActive && c.view().rows[1].midiActive && c.view().monitor.back().tracks == "Track 2");
    c.receive(keylab, uw::MidiMessage{{0x10F80000, 0, 0, 0}, 1});   // a MIDI clock tick
    c.tick();
    check("system messages go to no track", c.view().monitor.back().tracks.empty());

    // --- commands and undo
    c.setArmed(t1, true);
    c.setMuted(t2, true);
    c.setSoloed(t2, true);
    c.rename(t2, "Bass");
    c.rename(t2, "");
    c.tick();
    check("arm, mute, solo and a name", c.view().rows[0].armed && c.view().rows[1].muted && c.view().rows[1].soloed &&
                                            c.view().rows[1].name == "Bass" && c.view().undoLabel == "Rename track");
    c.undo();
    check("undo takes back the last command", c.view().rows[1].name == "Track 2" && c.view().redoLabel == "Rename track" &&
                                                 c.view().undoLabel == "Solo track");
    c.redo();
    check("redo puts it back", c.view().rows[1].name == "Bass" && c.view().redoLabel.empty());
    c.setMuted(t2, true);
    check("a command that changes nothing isn't one", c.view().undoLabel == "Rename track");

    const auto before = c.version();
    c.tick();
    check("a tick that changes nothing isn't a change", c.version() == before);

    // --- recording
    c.select(t1);
    c.record();
    clock += kBeat;   // a beat in: 120 a minute
    c.receive(keylab, uw::MidiMessage{{0x20903C64, 0, 0, 0}, 1});   // MIDI 1.0 note-on, C4
    clock += kBeat / 2;
    c.receive(keylab, uw::MidiMessage{{0x20803C40, 0, 0, 0}, 1});
    c.receive(fake.opened[MidiInputs::kVirtualMidi2], pitch725(61, 0x7A000000));
    c.receive(fake.opened[MidiInputs::kVirtualMidi2], perNoteReset(61));
    c.receive(fake.opened[MidiInputs::kVirtualMidi2], noteOn(0, 0, 64));   // held past the end
    clock += kBeat;
    c.tick();
    check("recording: the transport plays and records, and says where",
          c.view().transport.playing && c.view().transport.recording && c.view().status == "Recording to Track 1" &&
              c.view().transport.positionLabel == "1.3.3");
    check("the take shows on the armed track's lane as it grows",
          c.view().rows[0].clips.size() == 1 && c.view().rows[0].clips[0].recording &&
              c.view().rows[0].clips[0].length == kTicksPerQuarter * 5 / 2 && c.view().rows[0].clips[0].notes->size() == 2 &&
              c.view().rows[1].clips.empty());
    check("what's played still goes to the touched track, and to the armed one",
          c.view().monitor.back().tracks == "Track 1");
    c.stop();
    check("stopping makes the take a clip, as one command",
          c.tracks()[0].clips.size() == 1 && c.view().undoLabel == "Record" && !c.view().transport.playing &&
              !c.view().rows[0].clips[0].recording && c.view().rows[0].clips[0].name == "Track 1 1");
    {
        const auto clip = c.tracks()[0].clips[0].clip;
        const auto& e   = clip->events;
        check("the clip is in ticks from its start: a beat in, half a beat long",
              e.size() == 6 && e[0].tick == kTicksPerQuarter && e[1].tick == kTicksPerQuarter * 3 / 2 &&
                  clip->length == kTicksPerQuarter * 5 / 2);
        check("MIDI 1.0 is stored as MIDI 2.0",
              ump::messageType(e[0].message) == ump::MessageType::Midi2ChannelVoice && ump::isNoteOn(e[0].message) &&
                  ump::velocity16(e[0].message) == ump::scaleUp(100, 7, 16) &&
                  ump::messageType(e[1].message) == ump::MessageType::Midi2ChannelVoice);
        check("a per-key Pitch 7.25 with no note and a Per-Note Management reset are kept",
              e[2].message.words[0] == pitch725(61, 0).words[0] && e[2].message.words[1] == 0x7A000000 &&
                  e[3].message.words[0] == perNoteReset(61).words[0]);
        check("a note still held ends with the clip",
              ump::isNoteOn(e[4].message) && ump::isNoteOff(e[5].message) && ump::noteNumber(e[5].message) == 64 &&
                  e[5].tick == clip->length);
        check("the grid model shows its notes in sixteenths", [&] {
            GridModel grid;
            grid.showClip(clip);
            return grid.notes.size() == 2 && grid.notes[0].start == 4 && grid.notes[0].length == 2 &&
                   grid.notes[0].velocity == 100 && grid.steps == 16 && grid.clipName == "Track 1 1";
        }());
    }

    c.undo();
    check("undo takes the recording back", c.tracks()[0].clips.empty() && c.view().redoLabel == "Record");
    c.redo();
    const ClipRefId ref = c.tracks()[0].clips[0].id;

    c.locate(kTicksPerQuarter * 8);
    c.record();   // punch in at bar 3
    clock += kBeat;
    c.receive(keylab, noteOn(0, 0, 67));
    clock += kBeat;
    c.tick();
    c.record();   // punch out
    check("punching out keeps playing, with a second clip at bar 3",
          c.view().transport.playing && !c.view().transport.recording && c.tracks()[0].clips.size() == 2 &&
              c.tracks()[0].clips[1].start == kTicksPerQuarter * 8);
    c.stop();

    // A 31-EDO take, as KeyboardEDO sends it from its default anchor (A): the
    // step as the note number, the pitch as a Pitch 7.9 attribute.
    {
        c.locate(kTicksPerQuarter * 16);
        c.record();
        for (int step : {0, 3, 5, 8, 10, 13, 18}) {
            const auto pitch = static_cast<std::uint16_t>(std::lround((57 + step * 12.0 / 31) * 512));
            c.receive(fake.opened[MidiInputs::kVirtualMidi2],
                      ump::midi2NoteOn(0, 0, static_cast<std::uint8_t>(step + 40), 0x8000, pitch));
            clock += kBeat / 4;
            c.receive(fake.opened[MidiInputs::kVirtualMidi2], ump::midi2NoteOff(0, 0, static_cast<std::uint8_t>(step + 40)));
        }
        c.tick();
        c.stop();
        const ClipRef& take = c.tracks()[0].clips.back();
        check("a 31-EDO take's scale is worked out as it becomes a clip: 31-EDO, from an A",
              take.clip->scale == (Scale{31, 57}) && take.clip->scaleFit == ScaleFit::WorkedOut,
              std::to_string(take.clip->scale.edo) + " from " + std::to_string(take.clip->scale.referenceNote));
        check("the 12-EDO take's is 12-EDO", c.tracks()[0].clips[0].clip->scale == (Scale{12, 60}));
        const ClipRefId r       = take.id;
        const auto      before  = take.clip;
        c.setClipScale(r, Scale{19, 60});
        check("setting a clip's scale makes a new clip, set by hand, as one command",
              c.clip(r) != before && c.clip(r)->scale == (Scale{19, 60}) && c.clip(r)->scaleFit == ScaleFit::Chosen &&
                  c.clip(r)->id == before->id && c.clip(r)->events.size() == before->events.size() &&
                  c.view().undoLabel == "Set scale");
        c.undo();
        check("undo puts the worked-out scale back", c.clip(r) == before);
        c.deleteClip(r);
    }

    c.setArmed(t1, false);
    c.record();
    clock += kBeat;
    c.receive(keylab, noteOn(0, 0));
    c.tick();
    check("recording with nothing armed says so", c.view().warning && c.view().status == "Arm a track to record");
    c.stop();
    check("and records nothing", c.tracks()[0].clips.size() == 2 && c.tracks()[1].clips.empty());

    // --- clips
    c.selectClip(ref);
    check("selecting a clip touches its track", c.selectedClip() == ref && c.selected() == t1 && c.view().rows[0].clips[0].selected);
    c.moveClip(ref, kTicksPerQuarter * 4);
    check("a clip moves along its lane", c.tracks()[0].clips[0].start == kTicksPerQuarter * 4 && c.view().undoLabel == "Move clip");
    c.moveClip(ref, -100);
    check("not before the start", c.tracks()[0].clips[0].start == 0);
    c.undo();
    c.deleteClip(ref);
    check("and is deleted", c.tracks()[0].clips.size() == 1 && c.selectedClip() == 0 && c.view().undoLabel == "Delete clip");
    c.undo();
    check("undo brings it back where it was", c.tracks()[0].clips.size() == 2 && c.clip(ref) != nullptr &&
                                                  c.tracks()[0].clips[0].start == kTicksPerQuarter * 4);

    // --- the transport
    c.locate(0);
    c.setTempo(60);
    c.play();
    clock += 1'000'000'000;
    check("the tempo sets the speed", c.position() == kTicksPerQuarter && c.view().undoLabel == "Tempo");
    c.undo();
    clock += 500'000'000;
    check("undoing the tempo changes it from where it is", c.position() == kTicksPerQuarter * 2);
    c.setTempo(1e9);
    check("the tempo stays in range", c.session().tempoBpm == TrackController::kMaxTempo);
    c.stop();

    // --- tracks
    c.select(t1);
    c.removeTrack(t1);
    check("removing the touched track touches the next", c.tracks().size() == 1 && c.selected() == t2);
    c.undo();
    check("undo puts a removed track back, clips and all",
          c.tracks().size() == 2 && c.tracks()[0].id == t1 && c.tracks()[0].clips.size() == 2);
    c.removeTrack(t1);
    c.removeTrack(t2);
    check("the last track can go", c.tracks().empty() && c.selected() == 0 && c.view().rows.empty());
    check("track numbers aren't reused", c.view().rows.empty() && (c.addTrack(), c.view().rows[0].name == "Track 3"));
    c.undo();
    check("undoing an add touches no missing track", c.tracks().empty() && c.selected() == 0);

    for (int i = 0; i < 300; ++i) c.receive(keylab, uw::MidiMessage{{0x10F80000, 0, 0, 0}, 1});
    c.tick();
    check("the monitor keeps the latest lines", c.view().monitor.size() == TrackController::kMonitorLines);
    c.clearMonitor();
    check("and clears", c.view().monitor.empty());

    MidiInputs none(nullptr);
    none.openAll();
    TrackController quiet(none, noOutputs, noEngine);
    quiet.tick();
    check("no MIDI on this machine: still a track view", quiet.view().rows.empty() && quiet.view().midiBackend == "no MIDI" &&
                                                           quiet.view().listening == "no inputs");

    std::printf("%s: %d failure%s\n", failures == 0 ? "OK" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
