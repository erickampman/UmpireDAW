#include "App/ClipEdit.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "uw/ump/Ump.h"

namespace umpire {

namespace ump = uw::ump;

namespace {

constexpr std::size_t npos = NoteSpan::npos;

bool midi2(const uw::MidiMessage& m) {
    return ump::messageType(m) == ump::MessageType::Midi2ChannelVoice;
}

int keyOf(const uw::MidiMessage& m) {
    return (ump::group(m) * 16 + ump::channel(m)) * 128 + ump::noteNumber(m);
}

// A message for one key that isn't a note-on or note-off: per-note
// controllers, per-note pitch bend, Per-Note Management, poly pressure.
bool perNote(const uw::MidiMessage& m) {
    if (!ump::isChannelVoice(m)) return false;
    switch (ump::status(m)) {
    case ump::Status::PolyPressure: return true;
    case ump::Status::RegisteredPerNoteController:
    case ump::Status::AssignablePerNoteController:
    case ump::Status::PerNotePitchBend:
    case ump::Status::PerNoteManagement: return midi2(m);
    default: return false;
    }
}

bool pitch725(const uw::MidiMessage& m) {
    return midi2(m) && ump::status(m) == ump::Status::RegisteredPerNoteController && (m.words[0] & 0xFF) == 3;
}

void setKey(uw::MidiMessage& m, int note) {
    m.words[0] = (m.words[0] & ~0x7F00u) | static_cast<std::uint32_t>(note & 0x7F) << 8;
}

// A MIDI 1.0 note-on or note-off as MIDI 2.0, for what only 2.0 can say.
void toMidi2(uw::MidiMessage& m) {
    if (midi2(m) || (!ump::isNoteOn(m) && !ump::isNoteOff(m))) return;
    const auto g = ump::group(m), c = ump::channel(m), k = ump::noteNumber(m);
    m = ump::isNoteOn(m) ? ump::midi2NoteOn(g, c, k, ump::velocity16(m))
                         : ump::midi2NoteOff(g, c, k, static_cast<std::uint16_t>(ump::scaleUp(m.words[0] & 0x7F, 7, 16)));
}

void setAttribute(uw::MidiMessage& on, std::uint16_t pitch79) {
    toMidi2(on);
    on.words[0] = (on.words[0] & ~0xFFu) | static_cast<std::uint32_t>(ump::NoteAttribute::Pitch7_9);
    on.words[1] = (on.words[1] & 0xFFFF0000u) | pitch79;
}

void setVelocity(uw::MidiMessage& on, std::uint16_t velocity) {
    if (!midi2(on)) {
        // MIDI 1.0 keeps its protocol when 7 bits say it exactly.
        const auto v7 = ump::scaleDown(velocity, 16, 7);
        if (v7 > 0 && ump::scaleUp(v7, 7, 16) == velocity) {
            on.words[0] = (on.words[0] & ~0x7Fu) | v7;
            return;
        }
        toMidi2(on);
    }
    on.words[1] = static_cast<std::uint32_t>(velocity) << 16 | (on.words[1] & 0xFFFFu);
}

struct Span {
    int  key;   // group, channel and note
    Tick start, end;
};

// The free note number (no other note on it overlaps [start, end)) nearest
// `want`, in semitones; `prefer` first if it's free.
int freeKey(const std::vector<Span>& taken, int groupChannel, Tick start, Tick end, double want, int prefer) {
    const auto free = [&](int note) {
        for (const Span& s : taken)
            if (s.key == groupChannel * 128 + note && s.start < end && start < s.end) return false;
        return true;
    };
    if (prefer >= 0 && free(prefer)) return prefer;
    int    best = std::clamp(static_cast<int>(std::lround(want)), 0, 127);
    double dist = 1e9;
    for (int note = 0; note < 128; ++note)
        if (free(note) && std::abs(note - want) < dist) best = note, dist = std::abs(note - want);
    return best;
}

// An event of the new clip, before sorting: at one tick a moved note's
// note-off (and what was clamped to its end) comes first, then the clip's own
// events, then a moved note's start (what came just before its note-on, the
// note-on, what came with it).
struct Out {
    Tick            tick;
    int             bucket;   // 0 end, 1 the clip's, 2 start
    std::size_t     seq;
    uw::MidiMessage message;
    std::size_t     origin;   // its index in the old clip, or npos
    std::size_t     placed;   // which placed note this note-on is, or npos
};

} // namespace

bool sameMessage(const uw::MidiMessage& a, const uw::MidiMessage& b) noexcept {
    if (a.wordCount != b.wordCount) return false;
    for (int i = 0; i < a.wordCount && i < 4; ++i)
        if (a.words[i] != b.words[i]) return false;
    return true;
}

bool sameEvents(const std::vector<ClipEvent>& a, const std::vector<ClipEvent>& b) noexcept {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const ClipEvent& x, const ClipEvent& y) {
               return x.tick == y.tick && sameMessage(x.message, y.message);
           });
}

EditedClip editNotes(const Clip& clip, ClipId id, const std::vector<NoteChange>& changes, const std::vector<NewNote>& added) {
    const std::vector<ClipEvent>& events = clip.events;
    const std::vector<NoteSpan>   before = notesOf(events, clip.length);
    std::map<std::size_t, const NoteSpan*> byOn;
    for (const NoteSpan& n : before) byOn[n.on] = &n;

    // Which change each event goes with.
    std::vector<std::size_t> owner(events.size(), npos);
    std::vector<const NoteSpan*> changed(changes.size(), nullptr);
    for (std::size_t c = 0; c < changes.size(); ++c) {
        const auto it = byOn.find(changes[c].on);
        if (it == byOn.end() || owner[it->first] != npos) continue;
        const NoteSpan& n = *it->second;
        changed[c]        = &n;
        owner[n.on]       = c;
        if (n.off != npos) owner[n.off] = c;
        const int key = keyOf(events[n.on].message);
        // What came for its key at its start, just before it (back to a
        // note-off of that key: the note before's).
        for (std::size_t j = n.on; j-- > 0 && events[j].tick == n.start;) {
            const uw::MidiMessage& m = events[j].message;
            if (!ump::isChannelVoice(m) || keyOf(m) != key) continue;
            if (!perNote(m)) break;
            if (owner[j] == npos) owner[j] = c;
        }
        for (std::size_t j = n.on + 1; j < (n.off != npos ? n.off : events.size()); ++j)
            if (owner[j] == npos && perNote(events[j].message) && keyOf(events[j].message) == key) owner[j] = c;
    }

    std::vector<Out> out;
    out.reserve(events.size() + added.size() * 2);
    for (std::size_t j = 0; j < events.size(); ++j)
        if (owner[j] == npos) out.push_back({events[j].tick, 1, j, events[j].message, j, npos});

    // The notes left where they were hold their keys.
    std::vector<Span> taken;
    for (const NoteSpan& n : before)
        if (owner[n.on] == npos) taken.push_back({keyOf(events[n.on].message), n.start, n.start + n.length});

    std::vector<std::uint16_t> intended;   // each placed note's pitch
    std::size_t                extra = events.size();   // seq for what's new
    for (std::size_t c = 0; c < changes.size(); ++c) {
        const NoteSpan* n = changed[c];
        if (n == nullptr || changes[c].remove) continue;
        const NoteChange& ch    = changes[c];
        const Tick        start = std::max<Tick>(ch.start, 0), end = start + std::max<Tick>(ch.length, 1);
        const Tick        delta = start - n->start;
        const bool        moved = ch.pitch79 != n->pitch79;
        const uw::MidiMessage& on = events[n->on].message;
        const int gc = ump::group(on) * 16 + ump::channel(on);
        const int key = freeKey(taken, gc, start, end, ch.pitch79 / 512.0, moved ? -1 : n->note);
        taken.push_back({gc * 128 + key, start, end});
        const std::size_t placed = intended.size();
        intended.push_back(ch.pitch79);
        for (std::size_t j = 0; j < events.size(); ++j) {
            if (owner[j] != c) continue;
            uw::MidiMessage m = events[j].message;
            setKey(m, key);
            if (j == n->on) {
                if (ch.velocity != n->velocity) setVelocity(m, ch.velocity);
                if (moved && n->from == PitchFrom::Attribute) setAttribute(m, ch.pitch79);
                out.push_back({start, 2, j, m, j, placed});
            } else if (j == n->off) {
                out.push_back({end, 0, j, m, j, npos});
            } else if (j < n->on) {
                if (moved && pitch725(m)) m.words[1] = static_cast<std::uint32_t>(ch.pitch79) << 16;
                out.push_back({start, 2, j, m, j, npos});
            } else {
                const Tick t = std::min(events[j].tick + delta, end);
                out.push_back({t, t == end ? 0 : t == start ? 2 : 1, j, m, j, npos});
            }
        }
        if (n->off == npos) {   // held to the clip's end: it gets a note-off now
            uw::MidiMessage off = midi2(on) ? ump::midi2NoteOff(ump::group(on), ump::channel(on), static_cast<std::uint8_t>(key))
                                            : ump::midi1NoteOff(ump::group(on), ump::channel(on), static_cast<std::uint8_t>(key));
            out.push_back({end, 0, extra++, off, npos, npos});
        }
    }
    std::uint8_t group = 0, channel = 0;
    if (!before.empty()) {
        group   = ump::group(events[before.front().on].message);
        channel = ump::channel(events[before.front().on].message);
    }
    for (const NewNote& a : added) {
        const Tick start = std::max<Tick>(a.start, 0), end = start + std::max<Tick>(a.length, 1);
        const int  key   = freeKey(taken, group * 16 + channel, start, end, a.pitch79 / 512.0, -1);
        taken.push_back({(group * 16 + channel) * 128 + key, start, end});
        const auto k = static_cast<std::uint8_t>(key);
        out.push_back({start, 2, extra++, ump::midi2NoteOn(group, channel, k, std::max<std::uint16_t>(a.velocity, 1), a.pitch79),
                       npos, intended.size()});
        out.push_back({end, 0, extra++, ump::midi2NoteOff(group, channel, k), npos, npos});
        intended.push_back(a.pitch79);
    }

    std::stable_sort(out.begin(), out.end(), [](const Out& a, const Out& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.bucket != b.bucket) return a.bucket < b.bucket;
        return a.seq < b.seq;
    });

    auto made    = std::make_shared<Clip>();
    made->id     = id;
    made->name   = clip.name;
    made->scale  = clip.scale;
    made->scaleFit        = clip.scaleFit;
    made->scaleCandidates = clip.scaleCandidates;
    made->length = clip.length;
    EditedClip result;
    result.placed.assign(intended.size(), npos);
    std::vector<std::size_t> origin;
    made->events.reserve(out.size());
    for (const Out& o : out) {
        if (o.placed != npos) result.placed[o.placed] = made->events.size();
        origin.push_back(o.origin);
        made->events.push_back({o.tick, o.message});
        made->length = std::max(made->length, o.tick);
    }

    // Every note sounds where it should: a placed one at its pitch, the rest
    // as before. One that wouldn't gets an attribute.
    std::map<std::size_t, std::uint16_t> want;   // by note-on in the new clip
    for (std::size_t p = 0; p < result.placed.size(); ++p) want[result.placed[p]] = intended[p];
    for (std::size_t j = 0; j < origin.size(); ++j)
        if (origin[j] != npos && owner[origin[j]] == npos)
            if (const auto it = byOn.find(origin[j]); it != byOn.end()) want[j] = it->second->pitch79;
    for (const NoteSpan& n : notesOf(made->events, made->length)) {
        const auto it = want.find(n.on);
        if (it == want.end() || it->second == n.pitch79) continue;
        setAttribute(made->events[n.on].message, it->second);
        if (n.off != npos) toMidi2(made->events[n.off].message);
    }

    if (made->length == clip.length && sameEvents(made->events, events)) return {nullptr, {}};
    result.clip = std::move(made);
    return result;
}

} // namespace umpire
