#include "App/Clip.h"

#include <algorithm>
#include <map>

#include "uw/ump/Ump.h"

namespace umpire {

namespace ump = uw::ump;

std::vector<NoteSpan> notesOf(const std::vector<ClipEvent>& events, Tick end) {
    std::vector<NoteSpan> notes;
    std::map<int, std::size_t>   held;     // group, channel and note -> index into notes
    std::map<int, std::uint16_t> tuned;    // the same -> a Pitch 7.25, in 7.9
    const auto close = [&](int key, Tick at) {
        const auto it = held.find(key);
        if (it == held.end()) return;
        NoteSpan& n = notes[it->second];
        n.length    = std::max<Tick>(at - n.start, 0);
        held.erase(it);
    };
    for (const ClipEvent& e : events) {
        const uw::MidiMessage& m = e.message;
        if (!ump::isChannelVoice(m)) continue;
        const int key = (ump::group(m) * 16 + ump::channel(m)) * 128 + ump::noteNumber(m);
        const bool midi2 = ump::messageType(m) == ump::MessageType::Midi2ChannelVoice;
        if (ump::isNoteOn(m)) {
            close(key, e.tick);
            held[key] = notes.size();
            const bool attribute = midi2 && static_cast<ump::NoteAttribute>(m.words[0] & 0xFF) == ump::NoteAttribute::Pitch7_9;
            const auto t         = tuned.find(key);
            const auto pitch     = attribute || t == tuned.end() ? ump::notePitch79(m) : t->second;
            const auto from      = attribute ? PitchFrom::Attribute : t != tuned.end() ? PitchFrom::Pitch725 : PitchFrom::NoteNumber;
            notes.push_back(NoteSpan{e.tick, 0, ump::noteNumber(m), pitch, ump::velocity16(m), from});
        } else if (ump::isNoteOff(m)) {
            close(key, e.tick);
        } else if (midi2 && ump::status(m) == ump::Status::RegisteredPerNoteController && (m.words[0] & 0xFF) == 3) {
            tuned[key] = static_cast<std::uint16_t>(m.words[1] >> 16);   // Pitch 7.25: 7.9 is its top 16 bits
        } else if (midi2 && ump::status(m) == ump::Status::PerNoteManagement && (m.words[0] & 0x01) != 0) {
            tuned.erase(key);   // reset: the key's per-note controllers go back to their defaults
        }
    }
    while (!held.empty()) close(held.begin()->first, end);
    std::stable_sort(notes.begin(), notes.end(), [](const NoteSpan& a, const NoteSpan& b) { return a.start < b.start; });
    return notes;
}

void workOutScale(Clip& clip) {
    std::vector<std::uint16_t> pitches;
    for (const NoteSpan& n : notesOf(clip.events, clip.length)) pitches.push_back(n.pitch79);
    const ScaleGuess guess = workOutScale(pitches);
    clip.scale           = guess.scale;
    clip.scaleFit        = guess.fit;
    clip.scaleCandidates = guess.candidates;
}

} // namespace umpire
