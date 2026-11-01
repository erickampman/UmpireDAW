#include "App/ClipFile.h"

#include <cmath>
#include <cstring>

#include "uw/ump/Ump.h"

namespace umpire::clipfile {

namespace {

constexpr char kFileHeader[8] = {'S', 'M', 'F', '2', 'C', 'L', 'I', 'P'};

// Utility messages (M2-104-UM v1.1, 7.2): message type 0, the status in bits 23-20.
constexpr std::uint32_t kNoop  = 0x00000000u;
constexpr std::uint32_t kDctpq = 0x00300000u;   // ticks per quarter in bits 15-0
constexpr std::uint32_t kDcs   = 0x00400000u;   // ticks since the last event in bits 19-0
// UMP Stream (type 0xF, format 0): status 0x20 and 0x21 in bits 25-16.
constexpr std::uint32_t kStartOfClip = 0xF0200000u;
constexpr std::uint32_t kEndOfClip   = 0xF0210000u;
// Flex Data (type 0xD), complete in one UMP, to the group (addrs 1); status
// bank 0 (setup): Set Tempo 0x00, Set Time Signature 0x01.
constexpr std::uint32_t kSetTempo   = 0xD0100000u;
constexpr std::uint32_t kSetTimeSig = 0xD0100001u;

class Writer {
public:
    std::vector<std::uint8_t> bytes;

    void word(std::uint32_t w) {
        for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(static_cast<std::uint8_t>(w >> shift));
    }
    void message(const uw::MidiMessage& m) {
        const std::uint8_t n = uw::ump::wordCountForType(static_cast<std::uint8_t>(m.words[0] >> 28));
        for (std::uint8_t i = 0; i < n; ++i) word(m.words[i]);
    }
    void stream(std::uint32_t first) {
        word(first);
        word(0), word(0), word(0);
    }
    // A Delta Clockstamp `delta` ticks after the last: past 20 bits, a Null
    // message at each full one restarts the count (M2-116-U, 3.2.2).
    void delta(Tick ticks) {
        while (ticks > Tick{kMaxDelta}) {
            word(kDcs | kMaxDelta);
            word(kNoop);
            ticks -= kMaxDelta;
        }
        word(kDcs | static_cast<std::uint32_t>(ticks));
    }
};

std::uint32_t readWord(const std::uint8_t* p) {
    return std::uint32_t{p[0]} << 24 | std::uint32_t{p[1]} << 16 | std::uint32_t{p[2]} << 8 | std::uint32_t{p[3]};
}

} // namespace

std::vector<std::uint8_t> write(const std::vector<ClipEvent>& events, Tick length, const Tempo& tempo) {
    Writer w;
    w.bytes.assign(std::begin(kFileHeader), std::end(kFileHeader));
    // Clip Configuration Header: DCTPQ, after a Delta Clockstamp of 0.
    w.delta(0);
    w.word(kDctpq | static_cast<std::uint32_t>(kTicksPerQuarter));
    // Clip Sequence Data: Start of Clip, then the tempo and meter sharing its
    // Delta Clockstamp (7.1.1, 7.1.2).
    w.delta(0);
    w.stream(kStartOfClip);
    w.word(kSetTempo);
    w.word(static_cast<std::uint32_t>(std::lround(6e9 / tempo.bpm)));   // 10 ns units per quarter
    w.word(0), w.word(0);
    w.word(kSetTimeSig);
    w.word(static_cast<std::uint32_t>(tempo.beatsPerBar & 0xFF) << 24 | 2u << 16 | 8u << 8);   // n/4, 8 32nds a quarter
    w.word(0), w.word(0);
    Tick at = 0;
    for (const ClipEvent& e : events) {
        const Tick tick = e.tick < at ? at : e.tick;   // in time order; never backwards
        if (tick != at) w.delta(tick - at);
        at = tick;
        w.message(e.message);
    }
    w.delta(length > at ? length - at : 0);
    w.stream(kEndOfClip);
    return std::move(w.bytes);
}

Read read(const std::uint8_t* data, std::size_t size) {
    Read r;
    const auto fail = [&](std::string why) {
        r.error = std::move(why);
        r.events.clear();
        return r;
    };
    if (size < sizeof kFileHeader || std::memcmp(data, kFileHeader, sizeof kFileHeader) != 0)
        return fail("not a MIDI Clip File (no SMF2CLIP header)");
    if ((size - sizeof kFileHeader) % 4 != 0) return fail("cut short (not whole 32-bit words)");

    std::uint16_t tpq = 0;
    Tick          time = 0;    // in the file's ticks, from the first message
    Tick          start = -1;  // Start of Clip's time
    bool          ended = false;
    std::vector<std::pair<Tick, uw::MidiMessage>> found;   // the file's ticks
    for (std::size_t at = sizeof kFileHeader; at < size && !ended;) {
        uw::MidiMessage m;
        m.words[0]  = readWord(data + at);
        m.wordCount = uw::ump::wordCountForType(static_cast<std::uint8_t>(m.words[0] >> 28));
        if (at + m.wordCount * 4u > size) return fail("cut short (a message runs past the end)");
        for (std::uint8_t i = 1; i < m.wordCount; ++i) m.words[i] = readWord(data + at + i * 4u);
        at += m.wordCount * 4u;

        const std::uint32_t w0 = m.words[0];
        switch (w0 >> 28) {
            case 0x0:   // utility
                switch (w0 >> 20 & 0xF) {
                    case 0x3:
                        if (tpq == 0) tpq = static_cast<std::uint16_t>(w0 & 0xFFFF);
                        break;
                    case 0x4: time += w0 & kMaxDelta; break;
                    default: break;   // NOOP, JR clock and timestamp
                }
                continue;
            case 0xF:   // UMP Stream
                if ((w0 >> 26 & 0x3) == 0 && (w0 >> 16 & 0x3FF) == 0x20 && start < 0) start = time;
                else if ((w0 >> 26 & 0x3) == 0 && (w0 >> 16 & 0x3FF) == 0x21 && start >= 0) ended = true;
                continue;   // the rest (endpoint and function block messages) aren't a clip's
            case 0xD:   // Flex Data: the tempo and meter are the session's
                if ((w0 >> 8 & 0xFF) == 0x00 && (w0 & 0xFF) <= 0x01) {
                    if ((w0 & 0xFF) == 0x00 && r.tempoBpm == 0 && m.words[1] != 0) r.tempoBpm = 6e9 / m.words[1];
                    continue;
                }
                break;
            default: break;
        }
        if (tpq == 0) continue;   // before DCTPQ: Set Profile On, which Umpire doesn't act on
        found.emplace_back(start < 0 ? -1 : time, m);
    }
    if (tpq == 0) return fail("no ticks per quarter note (DCTPQ), or it's 0");
    if (start < 0) return fail("no Start of Clip");
    if (!ended) return fail("no End of Clip");

    // The file's ticks to Umpire's, rounded to the nearest; the Clip
    // Configuration Header's messages (-1) at 0.
    const auto ours = [tpq](Tick fileTicks) {
        return (fileTicks * kTicksPerQuarter + tpq / 2) / tpq;
    };
    for (const auto& [t, m] : found) r.events.push_back(ClipEvent{t < 0 ? 0 : ours(t - start), m});
    r.length          = ours(time - start);
    r.ticksPerQuarter = tpq;
    r.ok              = true;
    return r;
}

} // namespace umpire::clipfile
