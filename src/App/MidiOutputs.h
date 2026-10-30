#pragma once

// Umpire's MIDI outputs (milestone 7): the platform's output (ALSA sequencer
// UMP, CoreMIDI) through uw-core's MidiOutput seam, as MidiInputs is its
// input. It lists the destinations a track can play to, leaving out Umpire's
// own inputs (a track playing into them would loop), and opens one handle
// per stream, so a stream's scheduled messages can be cancelled on their own
// (uw::MidiOutput::cancelScheduled).
//
// It sends what it's given: the caller makes messages in the protocol a
// handle has (Player downconverts for a MIDI 1.0 device).

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "uw/midi/MidiOutput.h"

namespace umpire {

class MidiOutputs {
public:
    // One choice of output.
    struct Choice {
        std::string label;
        std::string id;   // uw::MidiDestinationInfo::id
        bool        hardware = false;
        bool operator==(const Choice&) const = default;
    };

    // `output` may be null (no MIDI on this machine): then there's nowhere to
    // play. Destinations whose id is one of `ownClients`, or starts with one
    // and ':', are Umpire's own (its inputs), and aren't offered.
    explicit MidiOutputs(uw::MidiOutput* output, std::vector<std::string> ownClients = {});
    ~MidiOutputs();

    MidiOutputs(const MidiOutputs&)            = delete;
    MidiOutputs& operator=(const MidiOutputs&) = delete;

    // Devices that came or went since the last call: open handles are
    // reconnected where their device is back. Returns true if anything
    // changed (the choices, say); generation() goes up with it.
    bool          syncDevices();
    std::uint64_t generation() const noexcept { return generations; }

    // A new handle to device `id`, or uw::kInvalidMidiDestination if it isn't
    // there now.
    uw::MidiDestinationHandle open(const std::string& id);
    void                      close(uw::MidiDestinationHandle handle);
    bool                      connected(uw::MidiDestinationHandle handle) const;

    uw::MidiProtocol protocol(uw::MidiDestinationHandle handle) const;
    bool             schedules(uw::MidiDestinationHandle handle) const;
    // At `when` on uw-core's clock, or now where the platform can't hold them
    // (schedules() false).
    void sendAt(uw::MidiDestinationHandle handle, const uw::MidiMessage* messages, std::size_t count,
                uw::Nanoseconds when);
    void cancel(uw::MidiDestinationHandle handle);

    const std::vector<Choice>& choices() const noexcept { return choiceList; }
    // How device `id` reads ("FluidSynth: Synth input port"); `connected`
    // false if it's absent now.
    std::string label(const std::string& id, bool* connected = nullptr) const;
    // Messages handed to the platform so far, for the view.
    std::uint64_t sent() const noexcept { return sentCount; }
    const char*   backendName() const noexcept { return output != nullptr ? output->name() : "no MIDI"; }

private:
    struct Open {
        std::string id;
        bool        connected = true;
    };

    void refreshChoices();
    bool own(const std::string& id) const;

    uw::MidiOutput*                           output;
    std::vector<std::string>                  ownClients;
    std::map<uw::MidiDestinationHandle, Open> opened;
    std::vector<Choice>                       choiceList;
    std::uint64_t                             seenChanges = 0;
    std::uint64_t                             generations = 0;
    std::uint64_t                             sentCount   = 0;
};

} // namespace umpire
