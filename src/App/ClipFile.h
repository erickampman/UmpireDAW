#pragma once

// A clip as a MIDI Clip File (M2-116-U v1.0, ".midi2"; milestone 9,
// docs/files.md): the File Header ("SMF2CLIP"), a Clip Configuration Header
// (DCTPQ), then the Clip Sequence Data, Start of Clip to End of Clip, every
// message after a Delta Clockstamp. Big-endian 32-bit words. No toolkit
// types, no file system: bytes in, bytes out (App/SessionFile does the files).
//
// Written at Umpire's own 40320 ticks to a quarter, which DCTPQ's 16 bits
// hold, so a clip's ticks go out and come back exactly. The session's tempo
// and meter go out as Set Tempo and Set Time Signature at Start of Clip (the
// spec's "should"), for other apps; reading drops them, since the session's
// tempo map rules Umpire's time. Read at any DCTPQ, rounded to Umpire's ticks.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "App/Clip.h"

namespace umpire::clipfile {

// The most ticks a Delta Clockstamp holds (20 bits). A longer gap is
// restarted with a Null message (M2-116-U, 3.2.2).
inline constexpr std::uint32_t kMaxDelta = 0xFFFFF;

struct Tempo {
    double bpm         = 120;
    int    beatsPerBar = 4;   // quarter notes: 4/4, 3/4, ...
};

// `events` (in time order, ticks from the clip's start) and `length` as a
// MIDI Clip File.
std::vector<std::uint8_t> write(const std::vector<ClipEvent>& events, Tick length, const Tempo& tempo);

struct Read {
    bool                   ok = false;
    std::string            error;   // why not, for the person opening it
    std::vector<ClipEvent> events;  // in Umpire's ticks, from Start of Clip
    Tick                   length = 0;   // Start of Clip to End of Clip
    std::uint16_t          ticksPerQuarter = 0;   // the file's DCTPQ
    double                 tempoBpm = 0;          // its first Set Tempo, 0 if none
};

// A MIDI Clip File's clip. Messages in the Clip Configuration Header (a
// program change, say) come first, at tick 0; Set Profile On, NOOPs, JR
// timestamps, Set Tempo and Set Time Signature are dropped.
Read read(const std::uint8_t* data, std::size_t size);

} // namespace umpire::clipfile
