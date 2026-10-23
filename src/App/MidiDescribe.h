#pragma once

#include <string>

#include "uw/ump/MidiMessage.h"

namespace umpire {

// One-line description of a UMP packet, e.g.
// "MIDI2 g0 ch1  NoteOn    note 61 vel 0x8000 pitch 61.312". StageHand's.
std::string describeUmp(const uw::MidiMessage& m);

} // namespace umpire
