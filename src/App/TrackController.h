#pragma once

// The track view's controller (as StageHand's RackController): it owns the
// session, its undo history and the transport, applies the widgets' intents,
// takes the MIDI that arrived, records it, and builds the TrackViewModel each
// tick. Main thread only; no toolkit types, so tests drive it headless with a
// fake MIDI input and a fake clock.
//
// Input belongs to the session (ui-direction.md): Umpire listens to every
// input. What's played goes to the touched track; while recording, it's
// recorded on every armed track.

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "App/History.h"
#include "App/MidiInputs.h"
#include "App/Session.h"
#include "App/TrackViewModel.h"
#include "App/Transport.h"
#include "uw/ump/Ump.h"

namespace umpire {

class TrackController {
public:
    // How long a track's activity light stays lit after its last message.
    static constexpr double kActivitySeconds = 0.15;
    static constexpr std::size_t kMonitorLines = 200;
    static constexpr double kMinTempo = 20, kMaxTempo = 300;

    // The time, on uw-core's clock (the one MIDI capture times use).
    using Clock = std::function<uw::Nanoseconds()>;

    explicit TrackController(MidiInputs& inputs, Clock clock = {});

    // Take what MIDI arrived, route and record it, and rebuild the view.
    void tick();
    const TrackViewModel& view() const noexcept { return model; }
    // Changes since the start: a window that drew `version()` needn't draw again.
    std::uint64_t version() const noexcept { return changes; }

    // A message as if it had just arrived from `source` (tests; the self-test).
    void receive(uw::MidiSourceHandle source, const uw::MidiMessage& message);

    // --- intents: the session (each one a command, so it can be undone) ----
    TrackId addTrack();   // after the touched track, touched; named "Track n"
    void    removeTrack(TrackId id);
    void    rename(TrackId id, const std::string& name);
    void    setArmed(TrackId id, bool armed);
    void    setMuted(TrackId id, bool muted);
    void    setSoloed(TrackId id, bool soloed);
    void    setTempo(double bpm);
    void    moveClip(ClipRefId ref, Tick start);
    void    deleteClip(ClipRefId ref);
    void    undo();
    void    redo();

    // --- intents: the view and the transport (not commands) -----------------
    void select(TrackId id);   // touch it: playing goes to it
    void selectClip(ClipRefId ref);   // 0: none; selects its track too
    void play();
    void stop();
    // Starts recording on the armed tracks, playing if stopped; while
    // recording, stops recording (punch out) and keeps playing.
    void record();
    void locate(Tick to);
    void clearMonitor();

    const Session&              session() const noexcept { return doc; }
    const std::vector<Track>&   tracks() const noexcept { return doc.tracks; }
    TrackId                     selected() const noexcept { return selectedId; }
    ClipRefId                   selectedClip() const noexcept { return selectedRef; }
    const Transport&            transport() const noexcept { return transportState; }
    Tick                        position() const { return transportState.tickAt(now()); }
    // The clip a reference places, or null.
    std::shared_ptr<const Clip> clip(ClipRefId ref) const;

private:
    // A take: what an armed track records while recording, until it becomes
    // a clip.
    struct Take {
        TrackId                track = 0;
        Tick                   start = 0;
        std::vector<ClipEvent> events;
        std::map<int, uw::MidiMessage> held;   // notes on, by group, channel and note
        std::shared_ptr<const std::vector<NoteSpan>> notes;   // for the view
        Tick                   notesEnd = -1;   // the length `notes` was made for
    };

    // A change to the session, named for Edit > Undo: applied, and recorded
    // in the history if it changed anything.
    template <typename Change>
    void command(const char* label, Change change);

    uw::Nanoseconds now() const { return clockFn(); }
    void   route(uw::MidiSourceHandle source, const uw::TimedMidiMessage& message);
    void   recordMessage(uw::MidiSourceHandle source, const uw::TimedMidiMessage& message);
    void   startTakes(Tick at);
    void   finishTakes(Tick at);
    void   rebuild();
    ClipModel clipModel(const ClipRef& ref);
    void   changed() { ++changes; }

    MidiInputs&                          inputs;
    Clock                                clockFn;
    Session                              doc;
    History                              history;
    Transport                            transportState;
    std::vector<Take>                    takes;
    std::map<uw::MidiSourceHandle, std::unique_ptr<uw::ump::UpconvertState>> upconverters;   // by input
    std::map<const Clip*, std::shared_ptr<const std::vector<NoteSpan>>>    clipNotes;      // the view's
    std::map<TrackId, uw::Nanoseconds>   lastActivity;
    TrackId                              nextId     = 1;
    int                                  nextNumber = 1;   // for "Track n"
    ClipRefId                            nextRef    = 1;
    ClipId                               nextClip   = 1;
    TrackId                              selectedId  = 0;
    ClipRefId                            selectedRef = 0;
    uw::Nanoseconds                      shownAt      = 0;   // when the view's position was last updated
    Tick                                 shownPosition = 0;
    std::vector<MidiInputs::Received>    arrived;
    std::deque<MonitorLine>              monitor;
    std::uint64_t                        monitorCount = 0;
    uw::Nanoseconds                      firstCapture = -1;
    TrackViewModel                       model;
    std::uint64_t                        changes = 0;
};

} // namespace umpire
