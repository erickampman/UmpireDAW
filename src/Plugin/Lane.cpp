#include "Plugin/Lane.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "uw/ump/Ump.h"

namespace umpire::plugin {

namespace ump = uw::ump;

namespace {

int noteKey(const uw::MidiMessage& m) {
    return (ump::group(m) * 16 + ump::channel(m)) * 128 + ump::noteNumber(m);
}

uw::MidiMessage noteOff(int key) {
    return ump::midi2NoteOff(static_cast<std::uint8_t>(key / 128 / 16), static_cast<std::uint8_t>(key / 128 % 16),
                             static_cast<std::uint8_t>(key % 128), 0);
}

bool isMidi2(const uw::MidiMessage& m) { return ump::messageType(m) == ump::MessageType::Midi2ChannelVoice; }

bool isPitch725(const uw::MidiMessage& m) {
    return isMidi2(m) && ump::status(m) == ump::Status::RegisteredPerNoteController && (m.words[0] & 0xFF) == 3;
}

bool isReset(const uw::MidiMessage& m) {
    return isMidi2(m) && ump::status(m) == ump::Status::PerNoteManagement && (m.words[0] & 0x01) != 0;
}

uw::MidiMessage resetKey(int key) {
    uw::MidiMessage m;
    m.words[0] = 0x40000000u | static_cast<std::uint32_t>(key / 128 / 16) << 24 | 0xFu << 20 |
                 static_cast<std::uint32_t>(key / 128 % 16) << 16 | static_cast<std::uint32_t>(key % 128) << 8 | 0x01;
    m.wordCount = 2;
    return m;
}

Tick spanEnd(const ClipRef& take) {
    Tick end = take.end();
    if (take.clip)   // a clip whose events run past its length still plays them
        for (const ClipEvent& e : take.clip->events) end = std::max(end, take.start + e.tick);
    return end;
}

struct Piece {
    Tick a = 0, b = 0;
    bool ownEnd = false;   // ends where the take does, not where a newer take begins
};

// [start, end] less [s, e) of each newer take.
std::vector<Piece> visiblePieces(Tick start, Tick end, const std::vector<std::pair<Tick, Tick>>& newer) {
    std::vector<Piece> pieces{{start, end, true}};
    for (const auto& [s, e] : newer) {
        if (e <= s) continue;
        std::vector<Piece> next;
        for (const Piece& p : pieces) {
            // The piece is [a, b] at the take's own end, else [a, b); the
            // newer take covers [s, e).
            if (e <= p.a || s > p.b || (s == p.b && !p.ownEnd)) {
                next.push_back(p);
                continue;
            }
            if (p.a < s) next.push_back({p.a, s, false});
            if (e < p.b || (e == p.b && p.ownEnd)) next.push_back({e, p.b, p.ownEnd});
        }
        pieces = std::move(next);
    }
    return pieces;
}

struct Placed {
    Tick            tick = 0;
    int             rank = 1;   // 0: ends a piece, so it comes first at its tick
    uw::MidiMessage message;
};

} // namespace

bool Lane::addTake(Tick start, Clip clip) {
    if (clip.events.empty()) return false;
    clip.id   = nextClip++;
    clip.name = "Take " + std::to_string(nextNumber++);
    workOutScale(clip);
    ClipRef ref;
    ref.id    = nextRef++;
    ref.start = start;
    ref.clip  = std::make_shared<const Clip>(std::move(clip));
    all.push_back(std::move(ref));
    return true;
}

void Lane::setTakes(std::vector<ClipRef> takes) {
    all.clear();
    nextNumber = 1;
    for (ClipRef& t : takes) {
        if (!t.clip) continue;
        Clip clip = *t.clip;
        clip.id   = nextClip++;
        clip.name = "Take " + std::to_string(nextNumber++);
        t.id      = nextRef++;
        t.clip    = std::make_shared<const Clip>(std::move(clip));
        all.push_back(std::move(t));
    }
}

std::vector<ClipEvent> flatten(const std::vector<ClipRef>& takes) {
    std::vector<Placed> out;
    for (std::size_t i = 0; i < takes.size(); ++i) {
        const ClipRef& take = takes[i];
        if (!take.clip || take.clip->events.empty()) continue;
        std::vector<std::pair<Tick, Tick>> newer;
        for (std::size_t j = i + 1; j < takes.size(); ++j)
            if (takes[j].clip) newer.emplace_back(takes[j].start, spanEnd(takes[j]));
        const Tick               start  = take.start;
        const std::vector<Piece> pieces = visiblePieces(start, spanEnd(take), newer);
        if (pieces.empty()) continue;

        std::vector<ClipEvent> events = take.clip->events;   // in time order, song ticks
        std::stable_sort(events.begin(), events.end(), [](const ClipEvent& x, const ClipEvent& y) { return x.tick < y.tick; });
        for (ClipEvent& e : events) e.tick += start;
        std::set<int> tunedKeys;
        for (const ClipEvent& e : events)
            if (isPitch725(e.message)) tunedKeys.insert(noteKey(e.message));

        for (const Piece& p : pieces) {
            std::map<int, const uw::MidiMessage*> tuning;   // in force before p.a
            std::set<int>                         sounding;
            std::size_t                           k = 0;
            for (; k < events.size() && events[k].tick < p.a; ++k) {
                const uw::MidiMessage& m = events[k].message;
                if (isPitch725(m)) tuning[noteKey(m)] = &m;
                else if (isReset(m)) tuning.erase(noteKey(m));
            }
            if (p.a > start)
                for (const int key : tunedKeys) {
                    const auto it = tuning.find(key);
                    out.push_back({p.a, 1, it != tuning.end() ? *it->second : resetKey(key)});
                }
            for (; k < events.size(); ++k) {
                const ClipEvent& e = events[k];
                if (e.tick > p.b || (e.tick == p.b && !p.ownEnd)) break;
                const uw::MidiMessage& m    = e.message;
                const int              rank = e.tick == p.b ? 0 : 1;
                if (ump::isNoteOn(m)) {
                    sounding.insert(noteKey(m));
                } else if (ump::isNoteOff(m)) {
                    if (sounding.erase(noteKey(m)) == 0) continue;   // began under a newer take
                }
                out.push_back({e.tick, rank, m});
            }
            for (const int key : sounding) out.push_back({p.b, 0, noteOff(key)});
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const Placed& x, const Placed& y) { return x.tick != y.tick ? x.tick < y.tick : x.rank < y.rank; });
    std::vector<ClipEvent> events;
    events.reserve(out.size());
    for (const Placed& p : out) events.push_back({p.tick, p.message});
    return events;
}

} // namespace umpire::plugin
