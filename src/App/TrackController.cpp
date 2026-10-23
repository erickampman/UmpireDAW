#include "App/TrackController.h"

#include <algorithm>
#include <cstdio>

#include "App/MidiDescribe.h"
#include "uw/base/Clock.h"
#include "uw/ump/Ump.h"

namespace umpire {

namespace ump = uw::ump;

TrackController::TrackController(MidiInputs& in) : inputs(in) {
    addTrack();   // a new session starts with one track, listening to everything
    rebuild();
}

Track* TrackController::find(TrackId id) {
    const auto it = std::find_if(trackList.begin(), trackList.end(), [id](const Track& t) { return t.id == id; });
    return it != trackList.end() ? &*it : nullptr;
}

// --- MIDI -------------------------------------------------------------------------

bool TrackController::takes(const Track& track, uw::MidiSourceHandle source, const uw::MidiMessage& m) const {
    if (!inputs.matches(track.input, source)) return false;
    const auto type = ump::messageType(m);
    // Utility and stream messages belong to no group.
    if (type == ump::MessageType::Utility || type == ump::MessageType::Stream) return false;
    if ((track.groupMask & (1u << ump::group(m))) == 0) return false;
    if (ump::isChannelVoice(m) && (track.channelMask & (1u << ump::channel(m))) == 0) return false;
    return true;
}

void TrackController::receive(uw::MidiSourceHandle source, const uw::MidiMessage& message) {
    arrived.push_back(MidiInputs::Received{source, uw::TimedMidiMessage{uw::nowNanoseconds(), message}});
}

void TrackController::route(uw::MidiSourceHandle source, const uw::TimedMidiMessage& timed) {
    std::string names;
    for (const Track& t : trackList) {
        if (!takes(t, source, timed.message)) continue;
        lastActivity[t.id] = now;
        if (!names.empty()) names += ", ";
        names += t.name;
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
    line.tracks = std::move(names);
    monitor.push_back(std::move(line));
    if (monitor.size() > kMonitorLines) monitor.pop_front();
    ++monitorCount;
}

void TrackController::tick(double t) {
    now = t;
    if (inputs.syncDevices()) changed();
    inputs.drain(arrived);
    for (const MidiInputs::Received& r : arrived) route(r.source, r.message);
    arrived.clear();
    rebuild();
}

// --- the view -----------------------------------------------------------------------

void TrackController::rebuild() {
    TrackViewModel next;
    next.rows.reserve(trackList.size());
    for (const Track& t : trackList) {
        TrackRowModel row;
        row.id          = t.id;
        row.name        = t.name;
        row.input       = t.input;
        row.inputLabel  = inputs.label(t.input, &row.inputConnected);
        row.groupMask   = t.groupMask;
        row.channelMask = t.channelMask;
        row.armed       = t.armed;
        row.muted       = t.muted;
        row.soloed      = t.soloed;
        row.selected    = t.id == selectedId;
        const auto it   = lastActivity.find(t.id);
        row.midiActive  = it != lastActivity.end() && now - it->second < kActivitySeconds;
        next.rows.push_back(std::move(row));
    }
    next.inputs       = inputs.choices();
    next.midiBackend  = inputs.backendName();
    next.midiDropped  = inputs.dropped();
    next.monitor.assign(monitor.begin(), monitor.end());
    next.monitorCount = monitorCount;
    if (!(next == model)) {
        model = std::move(next);
        changed();
    }
}

// --- intents ------------------------------------------------------------------------

TrackId TrackController::addTrack() {
    Track t;
    t.id   = nextId++;
    t.name = "Track " + std::to_string(nextNumber++);
    const auto at = std::find_if(trackList.begin(), trackList.end(), [this](const Track& x) { return x.id == selectedId; });
    trackList.insert(at == trackList.end() ? trackList.end() : std::next(at), t);
    selectedId = t.id;
    rebuild();
    return t.id;
}

void TrackController::removeTrack(TrackId id) {
    const auto it = std::find_if(trackList.begin(), trackList.end(), [id](const Track& t) { return t.id == id; });
    if (it == trackList.end()) return;
    const auto next = trackList.erase(it);
    lastActivity.erase(id);
    if (selectedId == id) {
        if (next != trackList.end()) selectedId = next->id;
        else selectedId = trackList.empty() ? 0 : trackList.back().id;
    }
    rebuild();
}

void TrackController::select(TrackId id) {
    if (find(id) == nullptr || id == selectedId) return;
    selectedId = id;
    rebuild();
}

void TrackController::rename(TrackId id, const std::string& name) {
    if (Track* t = find(id); t != nullptr && !name.empty()) t->name = name;
    rebuild();
}

void TrackController::setInput(TrackId id, const MidiSourceRef& input) {
    Track* t = find(id);
    if (t == nullptr) return;
    inputs.ensureOpen(input);
    t->input = input;
    rebuild();
}

void TrackController::setGroupMask(TrackId id, std::uint16_t mask) {
    if (Track* t = find(id)) t->groupMask = mask;
    rebuild();
}

void TrackController::setChannelMask(TrackId id, std::uint16_t mask) {
    if (Track* t = find(id)) t->channelMask = mask;
    rebuild();
}

void TrackController::setArmed(TrackId id, bool armed) {
    if (Track* t = find(id)) t->armed = armed;
    rebuild();
}

void TrackController::setMuted(TrackId id, bool muted) {
    if (Track* t = find(id)) t->muted = muted;
    rebuild();
}

void TrackController::setSoloed(TrackId id, bool soloed) {
    if (Track* t = find(id)) t->soloed = soloed;
    rebuild();
}

void TrackController::clearMonitor() {
    monitor.clear();
    firstCapture = -1;
    rebuild();
}

} // namespace umpire
