// The track view's controller (src/App/TrackController.h) over a fake MIDI
// input: tracks, intents, routing by input, group and channel, the activity
// lights and the monitor. Silent, no window, no MIDI hardware.

#include <cstdio>
#include <string>

#include "App/MidiInputs.h"
#include "App/TrackController.h"
#include "uw/ump/Ump.h"

namespace {

int failures = 0;

void check(const char* what, bool ok, const std::string& detail = {}) {
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str());
    if (!ok) ++failures;
}

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

} // namespace

int main() {
    using namespace umpire;

    FakeInput  fake;
    MidiInputs inputs(&fake);
    inputs.openAll();
    check("the virtual inputs and the hardware are opened, not the app's source",
          fake.opened.count(MidiInputs::kVirtualMidi2) == 1 && fake.opened.count("KeyLab:KeyLab MIDI") == 1 &&
              fake.opened.count("KeyEDO") == 0);
    check("the choices: All, None, hardware, software, the virtual inputs",
          inputs.choices().size() == 7 && inputs.choices()[2].label == "KeyLab" && inputs.choices()[4].label == "KeyEDO");

    TrackController c(inputs);
    c.tick(0);
    check("a new session has one track, selected, taking all inputs",
          c.view().rows.size() == 1 && c.view().rows[0].name == "Track 1" && c.view().rows[0].selected &&
              c.view().rows[0].inputLabel == "All inputs");

    const TrackId t1 = c.tracks()[0].id;
    const TrackId t2 = c.addTrack();
    check("adding a track puts it after the selected one, and selects it",
          c.tracks().size() == 2 && c.tracks()[1].id == t2 && c.selected() == t2 && c.view().rows[1].name == "Track 2");

    const uw::MidiSourceHandle keylab    = fake.opened["KeyLab:KeyLab MIDI"];
    const uw::MidiSourceHandle launchkey = fake.opened["Launchkey:Launchkey MIDI"];
    c.setInput(t2, {MidiSourceRef::Kind::Device, "Launchkey:Launchkey MIDI"});
    c.setChannelMask(t1, 1u << 0);   // channel 1 only

    // From the MIDI thread, as the platform input delivers.
    inputs.onMidi(launchkey, uw::TimedMidiMessage{1000, noteOn(0, 0)});
    c.tick(1.0);
    check("a message reaches every track whose input takes it",
          c.view().rows[0].midiActive && c.view().rows[1].midiActive, c.view().monitor.empty() ? "" : c.view().monitor.back().tracks);
    check("the monitor shows it, its source and the tracks",
          c.view().monitor.size() == 1 && c.view().monitor[0].source == "Launchkey" &&
              c.view().monitor[0].tracks == "Track 1, Track 2" && c.view().monitor[0].text.find("NoteOn") != std::string::npos);
    c.tick(1.0 + TrackController::kActivitySeconds + 0.01);
    check("the activity lights go out", !c.view().rows[0].midiActive && !c.view().rows[1].midiActive);

    c.receive(keylab, noteOn(0, 1));
    c.tick(2.0);
    check("another device, another channel: neither track takes it",
          !c.view().rows[0].midiActive && !c.view().rows[1].midiActive && c.view().monitor.back().tracks.empty());

    c.setGroupMask(t1, 1u << 3);
    c.setChannelMask(t1, 0xFFFF);
    c.receive(keylab, noteOn(0, 0));
    c.receive(keylab, noteOn(3, 5));
    c.tick(3.0);
    check("a group mask takes only its groups", c.view().monitor[c.view().monitor.size() - 2].tracks.empty() &&
                                                    c.view().monitor.back().tracks == "Track 1");

    c.setInput(t1, {MidiSourceRef::Kind::Device, "KeyEDO"});
    check("choosing an app's source opens it", fake.opened.count("KeyEDO") == 1);
    c.setInput(t1, {MidiSourceRef::Kind::Virtual, MidiInputs::kVirtualMidi2});
    c.setGroupMask(t1, 0xFFFF);
    c.receive(fake.opened[MidiInputs::kVirtualMidi2], noteOn(0, 0));
    c.tick(4.0);
    check("a virtual input", c.view().monitor.back().tracks == "Track 1" &&
                                 c.view().rows[0].inputLabel == "Umpire MIDI 2.0 In");

    c.setInput(t2, {MidiSourceRef::Kind::None, {}});
    c.receive(launchkey, noteOn(0, 0));
    c.tick(5.0);
    check("No input takes nothing", c.view().monitor.back().tracks.empty());

    c.setArmed(t1, true);
    c.setMuted(t2, true);
    c.setSoloed(t2, true);
    c.rename(t2, "Bass");
    c.rename(t2, "");
    c.tick(6.0);
    check("arm, mute, solo and a name", c.view().rows[0].armed && c.view().rows[1].muted && c.view().rows[1].soloed &&
                                            c.view().rows[1].name == "Bass");

    const auto before = c.version();
    c.tick(6.01);
    check("a tick that changes nothing isn't a change", c.version() == before);

    c.select(t1);
    c.removeTrack(t1);
    check("removing the selected track selects the next", c.tracks().size() == 1 && c.selected() == t2);
    c.removeTrack(t2);
    check("the last track can go", c.tracks().empty() && c.selected() == 0 && c.view().rows.empty());
    check("track numbers aren't reused", c.view().rows.empty() && (c.addTrack(), c.view().rows[0].name == "Track 3"));

    for (int i = 0; i < 300; ++i) c.receive(keylab, noteOn(0, 0));
    c.tick(7.0);
    check("the monitor keeps the latest lines", c.view().monitor.size() == TrackController::kMonitorLines);
    c.clearMonitor();
    check("and clears", c.view().monitor.empty());

    MidiInputs none(nullptr);
    none.openAll();
    TrackController quiet(none);
    quiet.tick(0);
    check("no MIDI on this machine: still a track view", quiet.view().rows.size() == 1 && quiet.view().midiBackend == "no MIDI");

    std::printf("%s: %d failure%s\n", failures == 0 ? "OK" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
