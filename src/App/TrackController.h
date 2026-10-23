#pragma once

// The track view's controller (as StageHand's RackController): it owns the
// tracks, applies the widgets' intents, takes the MIDI that arrived and routes
// it to the tracks, and builds the TrackViewModel each tick. Main thread only;
// no toolkit types, so tests drive it headless with a fake MIDI input.

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "App/MidiInputs.h"
#include "App/Track.h"
#include "App/TrackViewModel.h"

namespace umpire {

class TrackController {
public:
    // How long a track's activity light stays lit after its last message.
    static constexpr double kActivitySeconds = 0.15;
    static constexpr std::size_t kMonitorLines = 200;

    explicit TrackController(MidiInputs& inputs);

    // Take what MIDI arrived, route it, and rebuild the view. `now` in seconds,
    // any steady clock.
    void tick(double now);
    const TrackViewModel& view() const noexcept { return model; }
    // Changes since the start: a window that drew `version()` needn't draw again.
    std::uint64_t version() const noexcept { return changes; }

    // A message as if it had just arrived from `source` (tests; the self-test).
    void receive(uw::MidiSourceHandle source, const uw::MidiMessage& message);

    // --- intents ------------------------------------------------------------
    TrackId addTrack();   // after the selected track, selected; named "Track n"
    void    removeTrack(TrackId id);
    void    select(TrackId id);
    void    rename(TrackId id, const std::string& name);
    void    setInput(TrackId id, const MidiSourceRef& input);
    void    setGroupMask(TrackId id, std::uint16_t mask);
    void    setChannelMask(TrackId id, std::uint16_t mask);
    void    setArmed(TrackId id, bool armed);
    void    setMuted(TrackId id, bool muted);
    void    setSoloed(TrackId id, bool soloed);
    void    clearMonitor();

    const std::vector<Track>& tracks() const noexcept { return trackList; }
    TrackId                   selected() const noexcept { return selectedId; }

    // Whether `track` takes `message` from `source`: its input, group and channel.
    bool takes(const Track& track, uw::MidiSourceHandle source, const uw::MidiMessage& message) const;

private:
    Track* find(TrackId id);
    void   route(uw::MidiSourceHandle source, const uw::TimedMidiMessage& message);
    void   rebuild();
    void   changed() { ++changes; }

    MidiInputs&                          inputs;
    std::vector<Track>                   trackList;
    std::map<TrackId, double>            lastActivity;   // seconds, by track
    TrackId                              nextId     = 1;
    int                                  nextNumber = 1;   // for "Track n"
    TrackId                              selectedId = 0;
    double                               now        = 0;
    std::vector<MidiInputs::Received>    arrived;
    std::deque<MonitorLine>              monitor;
    std::uint64_t                        monitorCount = 0;
    uw::Nanoseconds                      firstCapture = -1;
    TrackViewModel                       model;
    std::uint64_t                        changes = 0;
};

} // namespace umpire
