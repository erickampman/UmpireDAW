// Editing a clip's notes (milestone 10, docs/clip-editor.md): ClipEdit's
// editNotes on its own, then the grid's edits as TrackController commands,
// with undo, two references to one clip, and save and reopen. Silent, no
// window, no MIDI hardware.

#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>

#include "App/AudioEngine.h"
#include "App/ClipEdit.h"
#include "App/GridModel.h"
#include "App/MainLoop.h"
#include "App/MidiInputs.h"
#include "App/SessionFile.h"
#include "App/TrackController.h"
#include "uw/ump/Ump.h"

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(const char* what, bool ok, const std::string& detail = {}) {
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str());
    if (!ok) ++failures;
}

class NoPlugins final : public uw::PluginFactory {
public:
    uw::PluginFormat                  format() const override { return uw::PluginFormat::Clap; }
    std::vector<uw::PluginDescriptor> scan() override { return {}; }
    std::unique_ptr<uw::HostedPlugin> create(const uw::PluginDescriptor&, uw::HostServices&) override { return nullptr; }
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

using umpire::Tick;
namespace ump = uw::ump;

constexpr Tick Q = umpire::kTicksPerQuarter, S = umpire::kGridStep;

uw::MidiMessage on(std::uint8_t key, std::uint16_t pitch79 = 0, std::uint16_t velocity = 0x8000) {
    return ump::midi2NoteOn(0, 0, key, velocity, pitch79);
}
uw::MidiMessage off(std::uint8_t key) {
    return ump::midi2NoteOff(0, 0, key);
}
uw::MidiMessage pitch725(std::uint8_t key, std::uint16_t pitch79) {
    return uw::MidiMessage{{0x40000000u | (std::uint32_t{key} << 8) | 3u, std::uint32_t{pitch79} << 16, 0, 0}, 2};
}
uw::MidiMessage perNoteBend(std::uint8_t key, std::uint32_t value) {
    return uw::MidiMessage{{0x40600000u | (std::uint32_t{key} << 8), value, 0, 0}, 2};
}
uw::MidiMessage perNoteController(std::uint8_t key, std::uint8_t index, std::uint32_t value) {
    return uw::MidiMessage{{0x40100000u | (std::uint32_t{key} << 8) | index, value, 0, 0}, 2};
}
uw::MidiMessage perNoteManagement(std::uint8_t key, std::uint8_t flags) {
    return uw::MidiMessage{{0x40F00000u | (std::uint32_t{key} << 8) | flags, 0, 0, 0}, 2};
}

bool attribute(const uw::MidiMessage& m) {
    return ump::messageType(m) == ump::MessageType::Midi2ChannelVoice &&
           static_cast<ump::NoteAttribute>(m.words[0] & 0xFF) == ump::NoteAttribute::Pitch7_9;
}

std::shared_ptr<umpire::Clip> clipOf(std::vector<umpire::ClipEvent> events, Tick length) {
    auto c    = std::make_shared<umpire::Clip>();
    c->id     = 7;
    c->name   = "Keys 1";
    c->length = length;
    c->events = std::move(events);
    return c;
}

// The change that leaves the note `n` as it is, to be edited.
umpire::NoteChange as(const umpire::NoteSpan& n) {
    return {n.on, false, n.start, n.length, n.pitch79, n.velocity};
}

// The events of key `key` (group 0, channel 0) with status `status`.
std::vector<umpire::ClipEvent> find(const umpire::Clip& c, std::uint8_t key, ump::Status status) {
    std::vector<umpire::ClipEvent> found;
    for (const auto& e : c.events)
        if (ump::isChannelVoice(e.message) && ump::status(e.message) == status && ump::noteNumber(e.message) == key)
            found.push_back(e);
    return found;
}

} // namespace

int main() {
    using namespace umpire;

    // --- editNotes ------------------------------------------------------------------------
    {
        // A keyboard's notes, upconverted: plain note numbers.
        const auto clip = clipOf({{0, on(60)}, {Q, off(60)}, {Q, on(64)}, {2 * Q, off(64)}}, 2 * Q);
        const auto notes = notesOf(clip->events, clip->length);
        check("nothing changed makes no clip", !editNotes(*clip, 8, {as(notes[0])}).clip);

        NoteChange later = as(notes[0]);
        later.start      = Q / 3;   // off the grid: a free drag
        const EditedClip timed = editNotes(*clip, 8, {later});
        const auto       t     = notesOf(timed.clip->events, timed.clip->length);
        check("moved in time: the same pitch and key, no attribute, the same length, a new clip with the id given",
              timed.clip && timed.clip->id == 8 && timed.clip->name == "Keys 1" && t.size() == 2 && t[0].start == Q / 3 &&
                  t[0].length == Q && t[0].note == 60 && t[0].pitch79 == 60 << 9 && !attribute(timed.clip->events[t[0].on].message));
        check("placed says where it is now", timed.placed.size() == 1 && timed.placed[0] == t[0].on &&
                                                 timed.clip->events[timed.placed[0]].message.words[0] == on(60).words[0]);
        check("the clip keeps its length when its notes end before it", timed.clip->length == 2 * Q);
        check("the other note is untouched", t[1].start == Q && t[1].note == 64 && t[1].pitch79 == 64 << 9);

        NoteChange up = as(notes[0]);
        up.pitch79    = 62 << 9;
        const EditedClip upped = editNotes(*clip, 8, {up});
        const auto       u     = notesOf(upped.clip->events, upped.clip->length);
        check("moved to a whole 12-EDO pitch: a plain note number, as it came", u[0].note == 62 && u[0].pitch79 == 62 << 9 &&
                                                                                 u[0].from == PitchFrom::NoteNumber);

        NoteChange onto = as(notes[0]);
        onto.start      = Q / 2;
        onto.pitch79    = 64 << 9;   // overlapping the note on 64
        const EditedClip shared = editNotes(*clip, 8, {onto});
        const auto       o      = notesOf(shared.clip->events, shared.clip->length);
        check("onto a key in use: the nearest free key, with a Pitch 7.9 attribute",
              o.size() == 2 && o[0].note != 64 && std::abs(o[0].note - 64) == 1 && o[0].pitch79 == 64 << 9 &&
                  o[0].from == PitchFrom::Attribute && o[1].note == 64 && o[1].start == Q);

        NoteChange louder = as(notes[1]);
        louder.velocity   = 0xFFFF;
        NoteChange gone   = as(notes[0]);
        gone.remove       = true;
        const EditedClip v  = editNotes(*clip, 8, {louder, gone});
        const auto       vn = notesOf(v.clip->events, v.clip->length);
        check("velocity in 16 bits, and a delete takes its note-on and note-off",
              vn.size() == 1 && vn[0].velocity == 0xFFFF && v.clip->events.size() == 2 && v.placed.size() == 1);

        const EditedClip added = editNotes(*clip, 8, {}, {NewNote{3 * Q, S, 67 * 512 + 100, 0x4000}, NewNote{0, S, 72 << 9, 0}});
        const auto       a     = notesOf(added.clip->events, added.clip->length);
        check("added notes: MIDI 2.0, a 7.9 attribute only off the 12-EDO notes, velocity at least 1",
              a.size() == 4 && a[1].note == 72 && a[1].from == PitchFrom::NoteNumber && a[1].velocity == 1 &&
                  a[3].start == 3 * Q && a[3].pitch79 == 67 * 512 + 100 && a[3].from == PitchFrom::Attribute &&
                  a[3].note == 67 && added.placed.size() == 2 && added.clip->length == 3 * Q + S &&
                  added.placed[1] == a[1].on);
        check("the clip grows to its last note's end", added.clip->length == 3 * Q + S);
    }
    {
        // A note with per-note data: Per-Note Management and a controller just
        // before its note-on, per-note pitch bend and a controller during it, and
        // a poly pressure past where it's to end.
        const auto clip = clipOf({{0, perNoteManagement(60, 0x02)},
                                  {0, perNoteController(60, 74, 0x40000000)},
                                  {0, on(60)},
                                  {Q / 4, perNoteBend(60, 0x90000000)},
                                  {Q / 2, perNoteController(60, 74, 0x60000000)},
                                  {3 * Q / 4, uw::MidiMessage{{0x40A03C00u, 0x12345678u, 0, 0}, 2}},
                                  {Q, off(60)},
                                  {Q, on(61)},
                                  {2 * Q, off(61)}},
                                 2 * Q);
        const auto notes = notesOf(clip->events, clip->length);
        NoteChange c     = as(notes[0]);
        c.start          = 2 * Q;
        c.length         = Q / 2;
        c.pitch79        = 67 << 9;
        const EditedClip e = editNotes(*clip, 8, {c});
        const Clip&      m = *e.clip;
        const auto bends = find(m, 67, ump::Status::PerNotePitchBend), ctls = find(m, 67, ump::Status::AssignablePerNoteController),
                   mgmt = find(m, 67, ump::Status::PerNoteManagement), poly = find(m, 67, ump::Status::PolyPressure);
        check("its per-note messages go with it, re-addressed to its new key",
              bends.size() == 1 && ctls.size() == 2 && mgmt.size() == 1 && poly.size() == 1 &&
                  find(m, 60, ump::Status::PerNotePitchBend).empty() && find(m, 60, ump::Status::PerNoteManagement).empty());
        check("in time with it: what came before its note-on still does",
              mgmt[0].tick == 2 * Q && ctls[0].tick == 2 * Q && bends[0].tick == 2 * Q + Q / 4 && ctls[1].tick == 2 * Q + Q / 2 &&
                  m.events[e.placed[0]].tick == 2 * Q && e.placed[0] > 0 &&
                  ump::status(m.events[e.placed[0] - 1].message) == ump::Status::AssignablePerNoteController);
        check("what was past its new end comes at its end, before its note-off",
              poly[0].tick == 2 * Q + Q / 2 && find(m, 67, ump::Status::NoteOff).at(0).tick == 2 * Q + Q / 2 &&
                  m.events.back().message.words[0] == off(67).words[0]);
        check("their values are as sent", bends[0].message.words[1] == 0x90000000u && poly[0].message.words[1] == 0x12345678u);
        const auto after = notesOf(m.events, m.length);
        check("the other note is as it was, now first", after.size() == 2 && after[0].note == 61 && after[0].start == Q &&
                                                           after[1].note == 67 && after[1].length == Q / 2);
    }
    {
        // KeyboardEDO's 31-EDO notes: pitch in a Pitch 7.9 attribute.
        const auto step31 = [](int s) { return static_cast<std::uint16_t>(std::lround((57 + s * 12.0 / 31) * 512)); };
        const auto clip   = clipOf({{0, on(57, step31(1))}, {Q, off(57)}}, Q);
        NoteChange c      = as(notesOf(clip->events, clip->length)[0]);
        c.pitch79         = step31(5);
        const EditedClip e = editNotes(*clip, 8, {c});
        const auto       n = notesOf(e.clip->events, e.clip->length);
        check("an attribute note keeps an attribute, rewritten, on the nearest key",
              n.size() == 1 && n[0].pitch79 == step31(5) && n[0].from == PitchFrom::Attribute &&
                  n[0].note == static_cast<int>(std::lround(step31(5) / 512.0)));
    }
    {
        // A Pitch 7.25 sent with its note (Retune's way), and one tuning a key earlier.
        const auto clip = clipOf({{Q / 2, pitch725(61, 61 * 512 + 128)},
                                  {Q, pitch725(62, 62 * 512 + 64)},
                                  {Q, on(62)},
                                  {2 * Q, off(62)},
                                  {2 * Q, on(61)},
                                  {3 * Q, off(61)}},
                                 3 * Q);
        const auto notes = notesOf(clip->events, clip->length);
        NoteChange with  = as(notes[0]);
        with.pitch79     = 62 * 512 + 200;
        const EditedClip e = editNotes(*clip, 8, {with});
        const auto       n = notesOf(e.clip->events, e.clip->length);
        check("a Pitch 7.25 sent with its note is rewritten, and no attribute is needed",
              n[0].pitch79 == 62 * 512 + 200 && n[0].from == PitchFrom::Pitch725 && n[0].note == 62 &&
                  find(*e.clip, 62, ump::Status::RegisteredPerNoteController).at(0).message.words[1] == (62u * 512 + 200) << 16);
        NoteChange early = as(notes[1]);
        early.start      = 0;   // before the 7.25 that tunes its key
        const EditedClip f = editNotes(*clip, 8, {early});
        const auto       m = notesOf(f.clip->events, f.clip->length);
        check("a note moved before its key's 7.25 keeps its pitch, by an attribute",
              m[0].start == 0 && m[0].note == 61 && m[0].pitch79 == 61 * 512 + 128 && m[0].from == PitchFrom::Attribute);
    }
    {
        // A glide: a 7.25 during note A on key 61 also tunes note B, later on 61.
        const auto clip = clipOf({{0, on(61)}, {Q / 2, pitch725(61, 61 * 512 + 256)}, {Q, off(61)}, {2 * Q, on(61)}, {3 * Q, off(61)}},
                                 3 * Q);
        const auto notes = notesOf(clip->events, clip->length);
        check("B's pitch comes from A's glide", notes[1].pitch79 == 61 * 512 + 256 && notes[1].from == PitchFrom::Pitch725);
        NoteChange gone  = as(notes[0]);
        gone.remove      = true;
        const EditedClip e = editNotes(*clip, 8, {gone});
        const auto       n = notesOf(e.clip->events, e.clip->length);
        check("deleting A takes its glide; B, untouched, keeps its pitch by an attribute",
              n.size() == 1 && n[0].pitch79 == 61 * 512 + 256 && n[0].from == PitchFrom::Attribute &&
                  find(*e.clip, 61, ump::Status::RegisteredPerNoteController).empty());
    }
    {
        // A file's MIDI 1.0 notes, and a note held to the end.
        const auto clip = clipOf({{0, ump::midi1NoteOn(0, 0, 60, 100)}, {Q, ump::midi1NoteOff(0, 0, 60)}, {Q, on(64)}}, 2 * Q);
        const auto notes = notesOf(clip->events, clip->length);
        NoteChange soft  = as(notes[0]);
        soft.velocity    = static_cast<std::uint16_t>(ump::scaleUp(64, 7, 16));
        NoteChange held  = as(notes[1]);
        held.length      = Q / 2;
        const EditedClip e = editNotes(*clip, 8, {soft, held});
        const auto       n = notesOf(e.clip->events, e.clip->length);
        check("a MIDI 1.0 note's velocity stays MIDI 1.0 when 7 bits say it",
              ump::messageType(e.clip->events[n[0].on].message) == ump::MessageType::Midi1ChannelVoice &&
                  (e.clip->events[n[0].on].message.words[0] & 0x7F) == 64);
        check("a note held to the end, resized, gets a note-off", n[1].length == Q / 2 && n[1].off != NoteSpan::npos &&
                                                                    e.clip->length == 2 * Q);
    }

    // --- the grid, editing a clip ------------------------------------------------------------
    {
        auto       clip = clipOf({{0, on(60)}, {Q, off(60)}, {Q, on(64)}, {2 * Q, off(64)}}, 2 * Q);
        std::shared_ptr<const Clip> current = clip;
        std::vector<std::string>    labels;
        GridModel m;
        m.showClip(current);
        m.editClip = [&](const std::string& label, const std::vector<NoteChange>& c, const std::vector<NewNote>& a) {
            EditedClip e = editNotes(*current, current->id + 1, c, a);
            if (e.clip) current = e.clip, labels.push_back(label);
            return e;
        };
        m.select(0);
        m.toggle(1);
        m.beginDrag();
        m.dragMove(S, 2);
        check("a drag shows at once, but isn't the clip's until let go",
              m.notes[0].pitch == 62 && notesOf(current->events, current->length)[0].note == 60);
        check("let go, it's one command on the clip", m.commit() && labels.back() == "Move notes" && current->id == 8 &&
                                                          m.clip() == current);
        const auto n = notesOf(current->events, current->length);
        check("the clip has the moved notes, and the grid shows them, still selected",
              n[0].start == S && n[0].note == 62 && n[1].note == 66 && m.notes.size() == 2 && m.selectionSize() == 2 &&
                  m.notes[0].on == n[0].on && m.notes[0].pitch == 62);
        m.select(1);
        m.setSelectedVelocity(127);
        m.commit();
        check("velocity: \"Set velocity\", the 16-bit value from the 7-bit view",
              labels.back() == "Set velocity" && notesOf(current->events, current->length)[1].velocity == 0xFFFF &&
                  m.selectionSize() == 1 && m.focus == 1);
        m.beginDrag();
        m.dragResize(-S, true);
        m.commit();
        check("a resize: \"Resize note\"", labels.back() == "Resize note" &&
                                               notesOf(current->events, current->length)[1].length == Q - S);
        m.addNote(70, 12);
        m.commit();
        check("a click on empty grid: \"Add note\", the new note selected",
              labels.back() == "Add note" && m.notes.size() == 3 && m.selectedNote() != nullptr &&
                  m.selectedNote()->pitch == 70 && m.selectedNote()->fromClip);
        m.beginDrag();
        m.dragMove(0, 0);
        const std::size_t count = labels.size();
        check("a drag that ends where it began makes no command", !m.commit() && labels.size() == count);
        m.select(0);
        m.deleteSelected();
        m.commit();
        check("Delete: \"Delete note\"", labels.back() == "Delete note" && m.notes.size() == 2);
    }
    {
        // A 31-EDO clip: a note moved a row up lands on the row's exact 7.9 pitch.
        const auto step31 = [](int s) { return static_cast<std::uint16_t>(std::lround((57 + s * 12.0 / 31) * 512)); };
        std::shared_ptr<const Clip> current;
        {
            auto c        = clipOf({{0, on(57, step31(0))}, {Q, off(57)}, {Q, on(58, step31(3))}, {2 * Q, off(58)}}, 2 * Q);
            c->scale      = Scale{31, 57};
            c->scaleFit   = ScaleFit::WorkedOut;
            current       = c;
        }
        GridModel m;
        m.showClip(current);
        m.editClip = [&](const std::string&, const std::vector<NoteChange>& c, const std::vector<NewNote>& a) {
            EditedClip e = editNotes(*current, 9, c, a);
            if (e.clip) current = e.clip;
            return e;
        };
        m.select(1);
        m.nudge(0, 1);
        m.commit();
        const auto n = notesOf(current->events, current->length);
        check("Pitch 7.9 on the EDO grid: a row up is the next 31-EDO step, exactly",
              n[1].pitch79 == step31(4) && n[1].from == PitchFrom::Attribute && m.notes[1].pitch == m.referenceRow() + 4 &&
                  current->scale == (Scale{31, 57}));
        m.nudge(Q, 0);
        m.commit();
        check("then moved in time it keeps that pitch", notesOf(current->events, current->length)[1].pitch79 == step31(4));
    }

    {
        // Quantize notes: the selected notes' starts onto the nearest sixteenth.
        std::shared_ptr<const Clip> current =
            clipOf({{S / 3, on(60)}, {S / 3 + S / 2, perNoteBend(60, 0x90000000)}, {S / 3 + S, off(60)},
                    {Q + S / 2 + 5, on(64)}, {Q + 2 * S, off(64)}, {3 * Q + 7, on(67)}, {3 * Q + S, off(67)}},
                   4 * Q);
        std::string label;
        GridModel   m;
        m.showClip(current);
        m.editClip = [&](const std::string& l, const std::vector<NoteChange>& c, const std::vector<NewNote>& a) {
            EditedClip e = editNotes(*current, 9, c, a);
            if (e.clip) current = e.clip, label = l;
            return e;
        };
        m.select(0);
        m.toggle(1);
        m.quantizeSelected();
        check("Quantize notes is one command, by that name", m.commit("Quantize notes") && label == "Quantize notes");
        const auto n = notesOf(current->events, current->length);
        check("the selected notes' starts on the nearest sixteenth, lengths kept, the rest alone",
              n[0].start == 0 && n[0].length == S && n[1].start == Q + S && n[1].length == S + S / 2 - 5 && n[2].start == 3 * Q + 7);
        check("per-note data moves with its note", find(*current, 60, ump::Status::PerNotePitchBend).at(0).tick == S / 2);
        check("both still selected", m.selectionSize() == 2);
        m.quantizeSelected();
        check("quantizing notes on the grid already is no command", !m.commit("Quantize notes"));
    }

    // --- TrackController: commands, undo, references, files ---------------------------------
    {
        const fs::path dir = fs::weakly_canonical(fs::temp_directory_path()) / ("umpire-clip-edit-tests-" + std::to_string(getpid()));
        fs::remove_all(dir);
        fs::create_directories(dir);
        // A session with two references to one clip, as a file would have it.
        {
            SavedSession saved;
            auto         clip = clipOf({{0, perNoteManagement(60, 0x01)}, {0, on(60)}, {Q / 2, perNoteBend(60, 0x88000000)},
                                        {Q, off(60)}, {Q, on(64)}, {2 * Q, off(64)}},
                                       2 * Q);
            clip->id = 5;
            Track t;
            t.id   = 1;
            t.name = "Keys";
            t.clips.push_back(ClipRef{1, clip, 0});
            t.clips.push_back(ClipRef{2, clip, 4 * Q});
            saved.session.tracks.push_back(t);
            saved.counters = SessionCounters{2, 2, 3, 6};
            check("a session with two references to one clip saves", saveSession((dir / "Two.umpire").string(), saved).ok);
        }
        FakeInput   fake;
        MidiInputs  inputs(&fake);
        MidiOutputs noOutputs(nullptr);
        NoPlugins   factory;
        MainLoop    loop;
        AudioEngine engine(nullptr, factory, loop);
        uw::Nanoseconds clock = 1'000'000'000;
        TrackController c(inputs, noOutputs, engine, [&clock] { return clock; });
        check("it opens", c.open((dir / "Two.umpire").string()).ok && c.tracks().at(0).clips.size() == 2);
        c.tick();
        const auto old   = c.clip(1);
        const auto notes = notesOf(old->events, old->length);
        NoteChange moved = as(notes[0]);
        moved.start      = Q / 4;
        moved.pitch79    = 62 << 9;
        const EditedClip e = c.editNotes(1, "Move note", {moved});
        check("an edit is a named command", e.clip && c.view().undoLabel == "Move note" && c.modified());
        check("with a new id, never used before", e.clip->id == 6 && c.clip(1)->id == 6);
        check("every reference to the clip sees it", c.clip(1) == e.clip && c.clip(2) == e.clip);
        const auto view = c.view().rows.at(0).clips;
        check("and the lanes draw its notes", view.size() == 2 && view[0].notes->at(0).note == 62 && view[1].notes->at(0).note == 62);
        check("nothing changed is no command", !c.editNotes(1, "Move note", {as(notesOf(e.clip->events, e.clip->length)[0])}).clip &&
                                                   c.view().undoLabel == "Move note");
        c.renameClip(2, "Verse");
        check("renaming keeps the id and the music, for both references",
              c.view().undoLabel == "Rename clip" && c.clip(1)->name == "Verse" && c.clip(1)->id == 6 &&
                  c.clip(1)->events.size() == e.clip->events.size() && c.clip(2) == c.clip(1));
        c.undo();
        c.undo();
        check("undo puts the old clip back, for both", c.clip(1) == old && c.clip(2) == old && c.view().redoLabel == "Move note");
        c.redo();
        c.redo();
        check("redo the new", c.clip(1)->id == 6 && c.clip(1)->name == "Verse");

        const std::string bundle = (dir / "Edited.umpire").string();
        check("it saves", c.save(bundle).ok);
        check("as a new clip file, the old one gone",
              fs::exists(dir / "Edited.umpire/clips/clip-6.midi2") && !fs::exists(dir / "Edited.umpire/clips/clip-5.midi2"));
        TrackController d(inputs, noOutputs, engine, [&clock] { return clock; });
        check("and reopens", d.open(bundle).ok);
        const auto back = d.clip(1);
        const auto bn   = back ? notesOf(back->events, back->length) : std::vector<NoteSpan>{};
        check("as edited: the note moved, its per-note data with it, the clip shared and named",
              back && d.clip(2) == back && back->name == "Verse" && back->id == 6 && bn.size() == 2 && bn[0].start == Q / 4 &&
                  bn[0].note == 62 && find(*back, 62, ump::Status::PerNotePitchBend).at(0).tick == Q / 4 + Q / 2 &&
                  find(*back, 62, ump::Status::PerNoteManagement).at(0).tick == Q / 4 && sameEvents(back->events, c.clip(1)->events));
        const ClipRefId ref = 1;
        check("an edit after reopening takes the next id", d.editNotes(ref, "Delete note", {{bn[1].on, true}}).clip &&
                                                                d.clip(1)->id == 7);
        c.deleteClip(1);
        c.deleteClip(2);
        check("an edit to a clip that's gone does nothing", !c.editNotes(1, "Move note", {moved}).clip);
        fs::remove_all(dir);
    }

    std::printf("%s: %d failure%s\n", failures == 0 ? "OK" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
