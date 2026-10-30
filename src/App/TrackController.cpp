#include "App/TrackController.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "App/MidiDescribe.h"
#include "uw/base/Clock.h"

namespace umpire {

namespace ump = uw::ump;

namespace {

// The view's position moves this often while playing, not every tick: about
// 30 frames a second.
constexpr uw::Nanoseconds kPositionInterval = 33'000'000;

int noteKey(const uw::MidiMessage& m) {
    return (ump::group(m) * 16 + ump::channel(m)) * 128 + ump::noteNumber(m);
}

std::string joined(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& n : names) out += (out.empty() ? "" : ", ") + n;
    return out;
}

} // namespace

TrackController::TrackController(MidiInputs& in, MidiOutputs& out, Clock c)
    : inputs(in), outputs(out), clockFn(c ? std::move(c) : Clock(uw::nowNanoseconds)), player(out) {
    // A new session is empty: it shows only the prompt (ui-direction.md).
    rebuild();
}

template <typename Change>
void TrackController::command(const char* label, Change change) {
    Session before = doc;
    change(doc);
    if (doc == before) return;
    history.record(label, std::move(before));
    rebuild();
}

std::shared_ptr<const Clip> TrackController::clip(ClipRefId ref) const {
    for (const Track& t : doc.tracks)
        for (const ClipRef& r : t.clips)
            if (r.id == ref) return r.clip;
    return nullptr;
}

// --- MIDI -------------------------------------------------------------------------

void TrackController::receive(uw::MidiSourceHandle source, const uw::MidiMessage& message) {
    arrived.push_back(MidiInputs::Received{source, uw::TimedMidiMessage{now(), message}});
}

void TrackController::route(uw::MidiSourceHandle source, const uw::TimedMidiMessage& timed) {
    std::vector<std::string> names;
    const auto took = [&](const Track& t) {
        lastActivity[t.id] = now();
        if (std::find(names.begin(), names.end(), t.name) == names.end()) names.push_back(t.name);
    };
    if (ump::isChannelVoice(timed.message)) {
        // "Play something to start": the first note makes the first track.
        if (doc.tracks.empty() && ump::isNoteOn(timed.message)) addTrack();
        if (const Track* t = doc.track(selectedId)) took(*t);
        // MIDI 1.0 is upconverted to MIDI 2.0 first, always, so the converter
        // follows each input's RPN and bank selections even while nothing
        // records or plays.
        uw::MidiMessage out[ump::kMaxTranslatedMessages];
        std::size_t     count = 1;
        out[0]                = timed.message;
        if (ump::messageType(timed.message) == ump::MessageType::Midi1ChannelVoice) {
            auto& state = upconverters[source];
            if (!state) state = std::make_unique<ump::UpconvertState>();
            count = ump::upconvert(timed.message, *state, out, std::size(out));
        }
        player.thru(selectedId, out, count);
        recordMessages(timed, out, count);
        for (const Take& take : takes)
            if (const Track* t = doc.track(take.track)) took(*t);
    }

    if (firstCapture < 0) firstCapture = timed.captureTime;
    MonitorLine line;
    line.seconds = static_cast<double>(timed.captureTime - firstCapture) / 1e9;
    line.source  = inputs.sourceName(source);
    for (std::uint8_t i = 0; i < timed.message.wordCount && i < 4; ++i) {
        char word[12];
        std::snprintf(word, sizeof word, "%s%08X", i == 0 ? "" : " ", timed.message.words[i]);
        line.words += word;
    }
    line.text   = describeUmp(timed.message);
    line.tracks = joined(names);
    monitor.push_back(std::move(line));
    if (monitor.size() > kMonitorLines) monitor.pop_front();
    ++monitorCount;
}

// A channel voice message, as MIDI 2.0 (`out`), into every take.
void TrackController::recordMessages(const uw::TimedMidiMessage& timed, const uw::MidiMessage* out, std::size_t count) {
    if (!transportState.recording()) return;
    const Tick at = transportState.tickAt(timed.captureTime);
    for (Take& take : takes) {
        const Tick tick = std::max<Tick>(0, at - take.start);
        for (std::size_t i = 0; i < count; ++i) {
            const auto pos = std::upper_bound(take.events.begin(), take.events.end(), tick,
                                              [](Tick t, const ClipEvent& e) { return t < e.tick; });
            take.events.insert(pos, ClipEvent{tick, out[i]});
            if (ump::isNoteOn(out[i])) take.held[noteKey(out[i])] = out[i];
            else if (ump::isNoteOff(out[i])) take.held.erase(noteKey(out[i]));
        }
    }
}

void TrackController::startTakes(Tick at) {
    takes.clear();
    for (const Track& t : doc.tracks)
        if (t.armed) takes.push_back(Take{t.id, at, {}, {}, nullptr, -1});
}

// The takes become clips, in one command ("Record"). A take with nothing in it
// leaves nothing; a note still held ends where the take does.
void TrackController::finishTakes(Tick at) {
    std::vector<std::pair<TrackId, ClipRef>> made;
    for (Take& take : takes) {
        const Track* t = doc.track(take.track);
        if (t == nullptr || take.events.empty()) continue;
        auto clip    = std::make_shared<Clip>();
        clip->id     = nextClip++;
        clip->name   = t->name + " " + std::to_string(t->clips.size() + 1);
        clip->length = std::max<Tick>({at - take.start, take.events.back().tick, 1});
        clip->events = std::move(take.events);
        for (const auto& [key, on] : take.held)
            clip->events.push_back(
                ClipEvent{clip->length, ump::midi2NoteOff(ump::group(on), ump::channel(on), ump::noteNumber(on), 0)});
        workOutScale(*clip);
        made.emplace_back(take.track, ClipRef{nextRef++, std::move(clip), take.start});
    }
    takes.clear();
    if (made.empty()) return;
    command("Record", [&](Session& s) {
        for (auto& [id, ref] : made)
            if (Track* t = s.track(id)) t->clips.push_back(std::move(ref));
    });
}

void TrackController::tick() {
    if (inputs.syncDevices()) changed();
    if (outputs.syncDevices()) changed();
    // Playback first, so what's played through goes by the session as it is
    // now (an output just set, a track just muted).
    player.tick(doc, transportState, now());
    inputs.drain(arrived);
    for (const MidiInputs::Received& r : arrived) route(r.source, r.message);
    arrived.clear();
    rebuild();
}

// --- the view -----------------------------------------------------------------------

ClipModel TrackController::clipModel(const ClipRef& ref) {
    ClipModel c;
    c.ref      = ref.id;
    c.name     = ref.clip->name;
    c.start    = ref.start;
    c.length   = ref.clip->length;
    c.selected = ref.id == selectedRef;
    auto& notes = clipNotes[ref.clip.get()];
    if (!notes) notes = std::make_shared<const std::vector<NoteSpan>>(notesOf(ref.clip->events, ref.clip->length));
    c.notes = notes;
    return c;
}

void TrackController::rebuild() {
    const uw::Nanoseconds t = now();

    // Undo can take away the touched track or the selected clip.
    if (doc.track(selectedId) == nullptr) selectedId = doc.tracks.empty() ? 0 : doc.tracks.front().id;
    if (selectedRef != 0 && !clip(selectedRef)) selectedRef = 0;
    // And change the tempo and the loop: the transport follows the session's.
    if (transportState.tempo() != doc.tempoBpm) transportState.setTempo(doc.tempoBpm, t);
    if (transportState.looping() != doc.looping || transportState.loopStart() != doc.loopStart ||
        transportState.loopEnd() != doc.loopEnd)
        transportState.setLoop(doc.looping, doc.loopStart, doc.loopEnd, t);

    const Tick live = transportState.tickAt(t);
    if (!transportState.playing() || t - shownAt >= kPositionInterval) {
        shownPosition = live;
        shownAt       = t;
    }

    TrackViewModel next;
    next.beatsPerBar = doc.beatsPerBar;
    next.rows.reserve(doc.tracks.size());
    for (const Track& track : doc.tracks) {
        TrackRowModel row;
        row.id         = track.id;
        row.name       = track.name;
        row.armed      = track.armed;
        row.muted      = track.muted;
        row.soloed     = track.soloed;
        row.touched    = track.id == selectedId;
        bool connected = true;
        row.outputLabel   = player.outputLabel(doc, track.id, &connected);
        row.outputMissing = !connected;
        const auto it  = lastActivity.find(track.id);
        row.midiActive = it != lastActivity.end() && static_cast<double>(t - it->second) < kActivitySeconds * 1e9;
        for (const ClipRef& ref : track.clips) row.clips.push_back(clipModel(ref));
        for (Take& take : takes) {
            if (take.track != track.id) continue;
            const Tick length = std::max<Tick>(shownPosition - take.start, take.events.empty() ? 0 : take.events.back().tick);
            if (take.notesEnd != length || !take.notes) {
                take.notes    = std::make_shared<const std::vector<NoteSpan>>(notesOf(take.events, length));
                take.notesEnd = length;
            }
            ClipModel c;
            c.name      = "Recording";
            c.start     = take.start;
            c.length    = length;
            c.recording = true;
            c.notes     = take.notes;
            row.clips.push_back(std::move(c));
        }
        for (ClipModel& c : row.clips) {
            if (c.notes->empty()) continue;
            const auto [lo, hi] = std::minmax_element(c.notes->begin(), c.notes->end(),
                                                      [](const NoteSpan& a, const NoteSpan& b) { return a.pitch79 < b.pitch79; });
            c.lowPitch  = lo->pitch79;
            c.highPitch = hi->pitch79;
        }
        next.rows.push_back(std::move(row));
    }

    TransportModel& tr = next.transport;
    tr.playing         = transportState.playing();
    tr.recording       = transportState.recording();
    tr.position        = shownPosition;
    tr.tempoBpm        = doc.tempoBpm;
    tr.looping         = doc.looping;
    tr.loopStart       = doc.loopStart;
    tr.loopEnd         = doc.loopEnd;
    {
        const Tick beat    = shownPosition / kTicksPerQuarter;
        const int  bar     = static_cast<int>(beat / doc.beatsPerBar) + 1;
        const int  inBar   = static_cast<int>(beat % doc.beatsPerBar) + 1;
        const int  sixteen = static_cast<int>(shownPosition % kTicksPerQuarter / (kTicksPerQuarter / 4)) + 1;
        tr.positionLabel   = std::to_string(bar) + "." + std::to_string(inBar) + "." + std::to_string(sixteen);
        const double seconds = static_cast<double>(shownPosition) / kTicksPerQuarter * 60.0 / doc.tempoBpm;
        const int    tenths  = static_cast<int>(seconds * 10);
        char         text[32];
        std::snprintf(text, sizeof text, "%d:%02d.%d", tenths / 600, tenths / 10 % 60, tenths % 10);
        tr.timeLabel = text;
    }

    const std::vector<std::string> listening = inputs.listening();
    next.listening = listening.empty() ? "no inputs" : joined(listening);
    if (transportState.recording()) {
        std::vector<std::string> armed;
        for (const Track& track : doc.tracks)
            if (track.armed) armed.push_back(track.name);
        next.warning = armed.empty();
        next.status  = armed.empty() ? "Arm a track to record" : "Recording to " + joined(armed);
    } else if (const Track* touched = doc.track(selectedId)) {
        next.status = "Playing goes to " + touched->name;
    } else {
        next.status = "Play something to start";
    }
    for (const MidiOutputs::Choice& c : outputs.choices()) next.outputs.push_back(OutputChoice{c.label, c.id, c.hardware});
    next.undoLabel    = history.undoLabel();
    next.redoLabel    = history.redoLabel();
    next.midiBackend  = inputs.backendName();
    next.midiDropped  = inputs.dropped();
    next.monitor.assign(monitor.begin(), monitor.end());
    next.monitorCount = monitorCount;
    if (!(next == model)) {
        model = std::move(next);
        changed();
    }
}

// --- intents: the session -----------------------------------------------------------

TrackId TrackController::addTrack() {
    const TrackId id = nextId++;
    const std::string name = "Track " + std::to_string(nextNumber++);
    const TrackId after = selectedId;
    selectedId = id;
    command("Add track", [&](Session& s) {
        Track t;
        t.id          = id;
        t.name        = name;
        const auto at = std::find_if(s.tracks.begin(), s.tracks.end(), [after](const Track& x) { return x.id == after; });
        s.tracks.insert(at == s.tracks.end() ? s.tracks.end() : std::next(at), std::move(t));
    });
    return id;
}

void TrackController::removeTrack(TrackId id) {
    const auto it = std::find_if(doc.tracks.begin(), doc.tracks.end(), [id](const Track& t) { return t.id == id; });
    if (it == doc.tracks.end()) return;
    if (selectedId == id) {   // the next track, or the one before the last
        if (std::next(it) != doc.tracks.end()) selectedId = std::next(it)->id;
        else selectedId = it == doc.tracks.begin() ? 0 : std::prev(it)->id;
    }
    lastActivity.erase(id);
    command("Remove track", [id](Session& s) {
        std::erase_if(s.tracks, [id](const Track& t) { return t.id == id; });
        std::erase_if(s.outputs, [id](const Output& o) { return o.track == id; });
    });
}

void TrackController::rename(TrackId id, const std::string& name) {
    if (name.empty()) return;
    command("Rename track", [&](Session& s) {
        if (Track* t = s.track(id)) t->name = name;
    });
}

void TrackController::setArmed(TrackId id, bool armed) {
    command(armed ? "Arm track" : "Disarm track", [&](Session& s) {
        if (Track* t = s.track(id)) t->armed = armed;
    });
}

void TrackController::setMuted(TrackId id, bool muted) {
    command(muted ? "Mute track" : "Unmute track", [&](Session& s) {
        if (Track* t = s.track(id)) t->muted = muted;
    });
}

void TrackController::setSoloed(TrackId id, bool soloed) {
    command(soloed ? "Solo track" : "Unsolo track", [&](Session& s) {
        if (Track* t = s.track(id)) t->soloed = soloed;
    });
}

void TrackController::setTempo(double bpm) {
    if (!std::isfinite(bpm)) return;
    bpm = std::clamp(bpm, kMinTempo, kMaxTempo);
    command("Tempo", [bpm](Session& s) { s.tempoBpm = bpm; });
}

void TrackController::setOutput(TrackId id, const std::string& device) {
    if (doc.track(id) == nullptr) return;
    command(device.empty() ? "No output" : "Set output", [&](Session& s) {
        std::erase_if(s.outputs, [id](const Output& o) { return o.track == id; });
        if (!device.empty()) s.outputs.push_back(Output{id, device});
    });
}

void TrackController::setLoop(Tick start, Tick end) {
    start = std::max<Tick>(0, std::min(start, end));
    end   = std::max({start + kTicksPerQuarter, end});
    command("Set loop", [&](Session& s) {
        s.loopStart = start;
        s.loopEnd   = end;
        s.looping   = true;
    });
}

void TrackController::setLooping(bool on) {
    if (on && doc.loopEnd <= doc.loopStart) {   // no range yet: the first bar
        setLoop(0, doc.beatsPerBar * kTicksPerQuarter);
        return;
    }
    command(on ? "Loop on" : "Loop off", [on](Session& s) { s.looping = on; });
}

void TrackController::moveClip(ClipRefId ref, Tick start) {
    command("Move clip", [&](Session& s) {
        for (Track& t : s.tracks)
            for (ClipRef& r : t.clips)
                if (r.id == ref) r.start = std::max<Tick>(0, start);
    });
}

// A clip never changes once made: the new scale is a copy (with the same id,
// so its grid window stays its own), and every reference to the old one moves
// to it.
void TrackController::setClipScale(ClipRefId ref, const Scale& scale) {
    const auto old = clip(ref);
    if (!old || (old->scale == scale && old->scaleFit == ScaleFit::Chosen)) return;
    auto copy      = std::make_shared<Clip>(*old);
    copy->scale    = Scale{std::clamp(scale.edo, Scale::kMinEdo, Scale::kMaxEdo), std::clamp(scale.referenceNote, 0, 127)};
    copy->scaleFit = ScaleFit::Chosen;
    const std::shared_ptr<const Clip> made = std::move(copy);
    command("Set scale", [&](Session& s) {
        for (Track& t : s.tracks)
            for (ClipRef& r : t.clips)
                if (r.clip == old) r.clip = made;
    });
}

void TrackController::deleteClip(ClipRefId ref) {
    command("Delete clip", [ref](Session& s) {
        for (Track& t : s.tracks) std::erase_if(t.clips, [ref](const ClipRef& r) { return r.id == ref; });
    });
}

void TrackController::undo() {
    if (history.undo(doc)) rebuild();
}

void TrackController::redo() {
    if (history.redo(doc)) rebuild();
}

// --- intents: the view and the transport ------------------------------------------

void TrackController::select(TrackId id) {
    if (doc.track(id) == nullptr || id == selectedId) return;
    selectedId = id;
    rebuild();
}

void TrackController::selectClip(ClipRefId ref) {
    selectedRef = 0;
    for (const Track& t : doc.tracks)
        for (const ClipRef& r : t.clips)
            if (r.id == ref) selectedRef = ref, selectedId = t.id;
    rebuild();
}

void TrackController::play() {
    if (transportState.playing()) return;
    transportState.play(now());
    player.started(doc, transportState, now());
    rebuild();
}

void TrackController::stop() {
    if (transportState.recording()) finishTakes(position());
    transportState.stop(now());
    player.stopped(transportState, now());
    rebuild();
}

void TrackController::record() {
    if (transportState.recording()) {
        finishTakes(position());
        transportState.setRecording(false, now());
    } else {
        const bool starting = !transportState.playing();
        transportState.play(now());
        transportState.setRecording(true, now());
        if (starting) player.started(doc, transportState, now());
        startTakes(position());
    }
    rebuild();
}

// While recording, a locate ends the takes and starts new ones from there.
void TrackController::locate(Tick to) {
    const bool recording = transportState.recording();
    if (recording) finishTakes(position());
    transportState.locate(to, now());
    if (transportState.playing()) player.started(doc, transportState, now());
    if (recording) startTakes(position());
    rebuild();
}

void TrackController::clearMonitor() {
    monitor.clear();
    firstCapture = -1;
    rebuild();
}

} // namespace umpire
