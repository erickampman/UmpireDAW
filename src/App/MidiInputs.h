#pragma once

// Umpire's MIDI inputs: the platform's input (ALSA sequencer UMP, CoreMIDI)
// through uw-core's MidiInput seam, opened the way StageHand opens them:
// every hardware input, plus two virtual inputs other apps (KeyEDO) connect
// to. Messages arrive on the MIDI thread and wait in a lock-free queue until
// the main thread drains them.
//
// MIDI 1.0 sources arrive as UMP MT 0x2 (uw-core wraps them losslessly), MIDI
// 2.0 sources as MT 0x4: both are UMP from here on.

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "uw/base/SpscQueue.h"
#include "uw/midi/MidiInput.h"

namespace umpire {

// An input, as the session would save a choice of one (a filter asked for
// through the palette, later: ui-direction.md). Copied from StageHand's
// Core/MidiSourceRef.h without its sequence kind.
struct MidiSourceRef {
    enum class Kind {
        All,       // every open input
        Device,    // one device, by its MidiSourceInfo id
        Virtual,   // one of Umpire's virtual inputs, by name
        None,      // no live input
    };
    Kind        kind = Kind::All;
    std::string name;   // Device: "KeyLab 88:KeyLab 88 MIDI 1"; Virtual: "MIDI 2.0 In"

    bool operator==(const MidiSourceRef&) const = default;
};

class MidiInputs final : public uw::MidiSink {
public:
    // The virtual inputs, as StageHand's.
    static constexpr const char* kVirtualMidi2 = "MIDI 2.0 In";
    static constexpr const char* kVirtualMidi1 = "MIDI 1.0 In";

    struct Received {
        uw::MidiSourceHandle source = uw::kInvalidMidiSource;
        uw::TimedMidiMessage message;
    };

    // One choice of input.
    struct Choice {
        std::string   label;
        MidiSourceRef ref;
        bool          hardware = false;
        bool operator==(const Choice&) const = default;
    };

    // `input` may be null (no MIDI on this machine): then nothing arrives.
    explicit MidiInputs(uw::MidiInput* input);
    ~MidiInputs() override;

    // Opens the virtual inputs and every hardware input.
    void openAll();
    // Devices that came or went since the last call: newly connected hardware
    // is opened. Returns true if anything changed (the choices, say).
    bool syncDevices();

    // Opens a device that isn't open yet (an app's own source, such as
    // KeyEDO's, isn't opened by openAll()).
    void ensureOpen(const MidiSourceRef& ref);

    // The main thread: everything queued since the last call, in order.
    void drain(std::vector<Received>& out);

    // The MIDI thread (uw::MidiSink); also what a test calls to inject a message.
    void onMidi(uw::MidiSourceHandle source, const uw::TimedMidiMessage& msg) noexcept override;

    // Whether a message from `source` is one `ref` names.
    bool matches(const MidiSourceRef& ref, uw::MidiSourceHandle source) const;
    // The open handle `ref` names, or kInvalidMidiSource.
    uw::MidiSourceHandle handleFor(const MidiSourceRef& ref) const;
    // A short name for a handle ("KeyLab 88", "MIDI 2.0 In"), for the monitor.
    std::string sourceName(uw::MidiSourceHandle source) const;
    // How an input reads; `connected` false if it's absent now.
    std::string label(const MidiSourceRef& ref, bool* connected = nullptr) const;
    // The names of the open inputs that are connected, hardware first, for the
    // status line ("KeyLab 88, MIDI 2.0 In, MIDI 1.0 In").
    std::vector<std::string> listening() const;

    const std::vector<Choice>& choices() const noexcept { return choiceList; }
    const char*   backendName() const noexcept { return input != nullptr ? input->name() : "no MIDI"; }
    std::uint64_t dropped() const noexcept { return droppedCount.load(std::memory_order_relaxed); }

private:
    struct Open {
        MidiSourceRef ref;
        std::string   name;   // human-readable
        bool          connected = true;
    };

    void refreshChoices();

    uw::MidiInput*                         input;
    std::map<uw::MidiSourceHandle, Open>   open;   // main thread only
    std::vector<uw::MidiSourceInfo>        enumerated;
    std::vector<Choice>                    choiceList;
    std::uint64_t                          seenChanges = 0;
    uw::SpscQueue<Received, 4096>          queue;
    std::atomic<std::uint64_t>             droppedCount{0};
};

} // namespace umpire
