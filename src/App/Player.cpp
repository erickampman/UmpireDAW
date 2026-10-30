#include "App/Player.h"

#include <algorithm>
#include <set>

#include "uw/base/Clock.h"
#include "uw/ump/Ump.h"

namespace umpire {

namespace ump = uw::ump;

namespace {

// A note scheduled this close to a cancel may have gone out already: it gets
// a note-off, which does no harm if it hadn't.
constexpr uw::Nanoseconds kCancelMargin = 1'000'000;

int noteKey(const uw::MidiMessage& m) {
    return (ump::group(m) * 16 + ump::channel(m)) * 128 + ump::noteNumber(m);
}

uw::MidiMessage noteOff(int key) {
    return ump::midi2NoteOff(static_cast<std::uint8_t>(key / 128 / 16), static_cast<std::uint8_t>(key / 128 % 16),
                             static_cast<std::uint8_t>(key % 128), 0);
}

bool isMidi2(const uw::MidiMessage& m) {
    return ump::messageType(m) == ump::MessageType::Midi2ChannelVoice;
}

// Pitch 7.25: MIDI 2.0 registered per-note controller 3.
bool isPitch725(const uw::MidiMessage& m) {
    return isMidi2(m) && ump::status(m) == ump::Status::RegisteredPerNoteController && (m.words[0] & 0xFF) == 3;
}

// Per-Note Management with its reset flag (S): the key's per-note
// controllers go back to their defaults.
bool isReset(const uw::MidiMessage& m) {
    return isMidi2(m) && ump::status(m) == ump::Status::PerNoteManagement && (m.words[0] & 0x01) != 0;
}

uw::MidiMessage reset(int key) {
    uw::MidiMessage m;
    m.words[0] = 0x40000000u | static_cast<std::uint32_t>(key / 128 / 16) << 24 | 0xFu << 20 |
                 static_cast<std::uint32_t>(key / 128 % 16) << 16 | static_cast<std::uint32_t>(key % 128) << 8 | 0x01;
    m.wordCount = 2;
    return m;
}

} // namespace

Player::Player(MidiOutputs& o) : outputs(o) {}

Player::~Player() {
    const uw::Nanoseconds now = uw::nowNanoseconds();
    for (auto& [id, s] : streams) {
        silence(s, now);
        thruOff(id);
        outputs.close(s.handle);
    }
}

// --- the transport ------------------------------------------------------------------

void Player::tick(const Session& session, const Transport& transport, uw::Nanoseconds now) {
    follow(session, transport, now);
    const bool playing = transport.playing();
    if (!playing && wasPlaying)   // stopped without stopped(): end what's sounding all the same
        for (auto& [id, s] : streams) silence(s, now);
    wasPlaying = playing;
    // A new anchor no call explained (a tempo, the loop, recording): what was
    // scheduled ahead is resent from the new one; notes sounding go on.
    if (playing && transport.anchor() != seenAnchor)
        for (auto& [id, s] : streams) reschedule(s, transport, now);
    seenAnchor = transport.anchor();
    if (!playing) return;

    for (auto& [id, s] : streams) {
        std::erase_if(s.sounding, [now](const Sounding& n) { return n.off <= now; });
        if (const Track* t = session.track(id); t != nullptr && s.audible && s.handle != uw::kInvalidMidiDestination)
            schedule(s, *t, transport, now);
    }
}

void Player::started(const Session& session, const Transport& transport, uw::Nanoseconds now) {
    follow(session, transport, now, false);
    for (auto& [id, s] : streams) {
        silence(s, now);
        const Track* t = session.track(id);
        if (t != nullptr && s.audible && s.handle != uw::kInvalidMidiDestination) join(s, *t, transport, now);
    }
    seenAnchor = transport.anchor();
    wasPlaying = transport.playing();
}

void Player::stopped(const Transport& transport, uw::Nanoseconds now) {
    for (auto& [id, s] : streams) silence(s, now);
    seenAnchor = transport.anchor();
    wasPlaying = false;
}

// --- the session ---------------------------------------------------------------------

// Each track with an output has a stream, its handle opened when its device
// is there. A stream whose device, audibility or clips change ends what it's
// sounding and, while playing, joins in again from the playhead (`rejoin`;
// started() joins them all itself).
void Player::follow(const Session& session, const Transport& transport, uw::Nanoseconds now, bool rejoin) {
    const bool devicesChanged = outputs.generation() != seenGeneration;
    seenGeneration            = outputs.generation();

    const auto drop = [&](std::map<TrackId, Stream>::iterator it) {
        silence(it->second, now);
        thruOff(it->first);
        outputs.close(it->second.handle);
        return streams.erase(it);
    };
    for (auto it = streams.begin(); it != streams.end();) {
        const std::string* device = session.outputOf(it->first);
        it = session.track(it->first) == nullptr || device == nullptr ? drop(it) : std::next(it);
    }

    const bool anySolo = std::any_of(session.tracks.begin(), session.tracks.end(), [](const Track& t) { return t.soloed; });
    for (const Track& t : session.tracks) {
        const std::string* device = session.outputOf(t.id);
        if (device == nullptr) continue;
        auto [it, made] = streams.try_emplace(t.id);
        Stream& s       = it->second;
        bool    retry   = made || devicesChanged;
        if (s.device != *device) {
            silence(s, now);
            thruOff(t.id);
            outputs.close(s.handle);
            s.handle = uw::kInvalidMidiDestination;
            s.device = *device;
            retry    = true;
        }
        bool fresh = false;
        if (s.handle == uw::kInvalidMidiDestination && retry) {
            s.handle = outputs.open(s.device);
            fresh    = s.handle != uw::kInvalidMidiDestination;
        }

        s.muted            = t.muted;
        const bool audible = !t.muted && (!anySolo || t.soloed);
        if (!fresh && audible == s.audible && s.clips == t.clips) continue;
        s.audible = audible;
        s.clips   = t.clips;
        if (!transport.playing() || !rejoin) continue;
        silence(s, now);
        if (audible && s.handle != uw::kInvalidMidiDestination) join(s, t, transport, now);
    }
}

// --- a stream ------------------------------------------------------------------------

// Plays from the playhead on: Pitch 7.25 chased there, now.
void Player::join(Stream& s, const Track& track, const Transport& transport, uw::Nanoseconds now) {
    chase(s, track, transport.tickAt(now), now);
    s.frontier = transport.rawAt(now);
}

// Drops what's scheduled and ends the notes sounding, now.
void Player::silence(Stream& s, uw::Nanoseconds now) {
    outputs.cancel(s.handle);
    std::vector<uw::MidiMessage> offs;
    for (const Sounding& n : s.sounding)
        if (n.on <= now + kCancelMargin && n.off > now) offs.push_back(noteOff(n.key));
    s.sounding.clear();
    send(s, offs.data(), offs.size(), now);
}

// Drops what's scheduled, to send it again from the transport's new anchor:
// notes that have started go on, and get their note-offs then.
void Player::reschedule(Stream& s, const Transport& transport, uw::Nanoseconds now) {
    outputs.cancel(s.handle);
    std::erase_if(s.sounding, [now](const Sounding& n) { return n.on > now; });
    for (Sounding& n : s.sounding)
        if (n.off > now) n.off = kNever;
    s.frontier = transport.rawAt(now);
}

// What the track's clips play from the stream's frontier to kLookahead from
// now (to now, where the platform can't hold messages), at their times. At a
// loop wrap, the notes sounding end and Pitch 7.25 is chased at the loop's
// start.
void Player::schedule(Stream& s, const Track& track, const Transport& transport, uw::Nanoseconds now) {
    const Tick to = transport.rawAt(now + (outputs.schedules(s.handle) ? kLookahead : 0));
    if (to <= s.frontier) return;
    struct Due {
        Tick                   tick = 0;
        const uw::MidiMessage* message = nullptr;
    };
    std::vector<Due> due;
    for (const Transport::Run& run : transport.runs(s.frontier, to)) {
        if (run.wrapped) {
            const uw::Nanoseconds at = transport.timeOf(run, run.begin);
            endSounding(s, at);
            chase(s, track, run.begin, at);
        }
        due.clear();
        for (const ClipRef& ref : track.clips) {
            if (!ref.clip || ref.end() < run.begin || ref.start >= run.end) continue;
            const std::vector<ClipEvent>& events = ref.clip->events;
            auto it = std::lower_bound(events.begin(), events.end(), run.begin - ref.start,
                                       [](const ClipEvent& e, Tick t) { return e.tick < t; });
            for (; it != events.end() && ref.start + it->tick < run.end; ++it)
                due.push_back(Due{ref.start + it->tick, &it->message});
        }
        std::stable_sort(due.begin(), due.end(), [](const Due& a, const Due& b) { return a.tick < b.tick; });
        for (const Due& d : due) {
            const uw::Nanoseconds  at = transport.timeOf(run, d.tick);
            const uw::MidiMessage& m  = *d.message;
            if (ump::isNoteOn(m)) {
                s.sounding.push_back(Sounding{noteKey(m), at, kNever});
            } else if (ump::isNoteOff(m)) {
                const int key = noteKey(m);
                for (Sounding& n : s.sounding)
                    if (n.key == key && n.off == kNever) {
                        n.off = at;
                        break;
                    }
            }
            send(s, &m, 1, at);
        }
    }
    s.frontier = to;
}

// Each key the track's clips tune: the Pitch 7.25 in force at tick `at`, or a
// reset where none is (none yet, or reset since), sent at `when`.
void Player::chase(Stream& s, const Track& track, Tick at, uw::Nanoseconds when) {
    struct Tuning {
        Tick                   tick = 0;
        const uw::MidiMessage* message = nullptr;
    };
    std::vector<Tuning> tunings;
    std::set<int>       tuned;
    for (const ClipRef& ref : track.clips) {
        if (!ref.clip) continue;
        for (const ClipEvent& e : ref.clip->events) {
            if (isPitch725(e.message)) tuned.insert(noteKey(e.message));
            if (isPitch725(e.message) || isReset(e.message)) tunings.push_back(Tuning{ref.start + e.tick, &e.message});
        }
    }
    if (tuned.empty()) return;
    std::stable_sort(tunings.begin(), tunings.end(), [](const Tuning& a, const Tuning& b) { return a.tick < b.tick; });
    std::map<int, const uw::MidiMessage*> inForce;
    for (const Tuning& t : tunings) {
        if (t.tick >= at) break;
        if (isPitch725(*t.message)) inForce[noteKey(*t.message)] = t.message;
        else inForce.erase(noteKey(*t.message));
    }
    std::vector<uw::MidiMessage> chased;
    for (const int key : tuned) {
        const auto it = inForce.find(key);
        chased.push_back(it != inForce.end() ? *it->second : reset(key));
    }
    send(s, chased.data(), chased.size(), when);
}

// The notes sounding end at `when` (a loop wrap).
void Player::endSounding(Stream& s, uw::Nanoseconds when) {
    std::vector<uw::MidiMessage> offs;
    for (Sounding& n : s.sounding)
        if (n.off > when) {
            offs.push_back(noteOff(n.key));
            n.off = when;
        }
    send(s, offs.data(), offs.size(), when);
}

// Clips are MIDI 2.0; a MIDI 1.0 device gets them downconverted, without what
// MIDI 1.0 can't say (per-note data, Pitch 7.25 and 7.9 among it).
void Player::send(Stream& s, const uw::MidiMessage* messages, std::size_t count, uw::Nanoseconds when) {
    if (count == 0 || s.handle == uw::kInvalidMidiDestination) return;
    if (outputs.protocol(s.handle) == uw::MidiProtocol::Midi2) {
        outputs.sendAt(s.handle, messages, count, when);
        return;
    }
    std::vector<uw::MidiMessage> out;
    uw::MidiMessage              converted[ump::kMaxTranslatedMessages];
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t n = ump::downconvert(messages[i], converted, std::size(converted));
        out.insert(out.end(), converted, converted + n);
    }
    outputs.sendAt(s.handle, out.data(), out.size(), when);
}

// --- played live ----------------------------------------------------------------------

void Player::thru(TrackId track, const uw::MidiMessage* messages, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        const uw::MidiMessage& m = messages[i];
        TrackId to = track;
        if (ump::isChannelVoice(m) && ump::isNoteOff(m)) {
            const auto held = thruHeld.find(noteKey(m));
            if (held != thruHeld.end()) {
                to = held->second;
                thruHeld.erase(held);
                if (const auto it = streams.find(to); it != streams.end()) send(it->second, &m, 1, 0);
                continue;
            }
        }
        const auto it = streams.find(to);
        if (it == streams.end() || it->second.muted || it->second.handle == uw::kInvalidMidiDestination) continue;
        if (ump::isChannelVoice(m) && ump::isNoteOn(m)) thruHeld[noteKey(m)] = to;
        send(it->second, &m, 1, 0);
    }
}

// Live notes held on `track`'s output end (it's going, or going elsewhere).
void Player::thruOff(TrackId track) {
    const auto it = streams.find(track);
    for (auto held = thruHeld.begin(); held != thruHeld.end();) {
        if (held->second != track) {
            ++held;
            continue;
        }
        if (it != streams.end()) {
            const uw::MidiMessage off = noteOff(held->first);
            send(it->second, &off, 1, 0);
        }
        held = thruHeld.erase(held);
    }
}

std::string Player::outputLabel(const Session& session, TrackId track, bool* connected) const {
    const std::string* device = session.outputOf(track);
    if (device == nullptr) {
        if (connected != nullptr) *connected = true;
        return "No output";
    }
    return outputs.label(*device, connected);
}

} // namespace umpire
