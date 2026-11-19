#include "Plugin/LaneKeeper.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace umpire::plugin {

LaneKeeper::LaneKeeper(PluginEngine& e, TakeAdded added) : engine(e), onTake(std::move(added)) {}

LaneKeeper::~LaneKeeper() { stop(); }

void LaneKeeper::start() {
    if (thread.joinable()) return;
    {
        std::lock_guard<std::mutex> guard(wakeLock);
        quit = false;
    }
    thread = std::thread([this] { run(); });
}

void LaneKeeper::stop() {
    if (!thread.joinable()) return;
    {
        std::lock_guard<std::mutex> guard(wakeLock);
        quit = true;
    }
    wake.notify_all();
    thread.join();
}

void LaneKeeper::run() {
    std::unique_lock<std::mutex> sleeping(wakeLock);
    while (!quit) {
        sleeping.unlock();
        pump();
        sleeping.lock();
        wake.wait_for(sleeping, std::chrono::milliseconds(10), [this] { return quit; });
    }
}

bool LaneKeeper::pump() {
    std::lock_guard<std::mutex> guard(lock);
    return pumpLocked();
}

bool LaneKeeper::pumpLocked() {
    bool         added = false;
    FinishedTake take;
    while (engine.collectTake(take)) {
        if (take.clip.events.empty()) continue;   // nothing in it: no take, no step
        recordLocked("Record take");
        lane.addTake(take.start, std::move(take.clip));
        added = true;
        if (onTake) onTake(lane.takes().back());
    }
    if (added) changedLocked();
    else engine.collectGarbage();
    return added;
}

void LaneKeeper::recordLocked(std::string label) {
    undos.push_back({std::move(label), lane.takes()});
    if (undos.size() > kUndoLimit) undos.erase(undos.begin());
    redos.clear();
}

void LaneKeeper::changedLocked() {
    engine.setEvents(flatten(lane.takes()));
    dirty.store(true);
    changes.fetch_add(1);
}

LaneKeeper::View LaneKeeper::view() const {
    std::lock_guard<std::mutex> guard(lock);
    View v;
    v.version   = changes.load();
    v.takes     = lane.takes();
    v.undoLabel = undos.empty() ? std::string() : undos.back().label;
    v.redoLabel = redos.empty() ? std::string() : redos.back().label;
    return v;
}

LaneKeeper::Edited LaneKeeper::editTake(ClipRefId ref, const std::string& label,
                                        const std::vector<NoteChange>& noteChanges, const std::vector<NewNote>& added) {
    std::lock_guard<std::mutex> guard(lock);
    pumpLocked();
    const ClipRef* take = lane.find(ref);
    if (take == nullptr || !take->clip) return {};
    // Notes moved (or added) before the take's start: the clip is shifted
    // later by as much, and the take starts earlier, so nothing goes below 0.
    Tick lead = 0;
    for (const NoteChange& c : noteChanges)
        if (!c.remove) lead = std::min(lead, c.start);
    for (const NewNote& n : added) lead = std::min(lead, n.start);
    Clip                    base = *take->clip;
    std::vector<NoteChange> moved = noteChanges;
    std::vector<NewNote>    adds  = added;
    if (lead < 0) {
        for (ClipEvent& e : base.events) e.tick -= lead;
        base.length -= lead;
        for (NoteChange& c : moved) c.start -= lead;
        for (NewNote& n : adds) n.start -= lead;
    }
    Edited made;
    made.edit = umpire::editNotes(base, lane.newClipId(), moved, adds);
    if (!made.edit.clip) return {};
    made.start = take->start + lead;
    recordLocked(label);
    lane.replace(ref, made.start, made.edit.clip);
    changedLocked();
    return made;
}

bool LaneKeeper::deleteTake(ClipRefId ref) {
    std::lock_guard<std::mutex> guard(lock);
    pumpLocked();
    if (lane.find(ref) == nullptr) return false;
    recordLocked("Delete take");
    lane.remove(ref);
    changedLocked();
    return true;
}

bool LaneKeeper::bringToFront(ClipRefId ref) {
    std::lock_guard<std::mutex> guard(lock);
    pumpLocked();
    const std::vector<ClipRef>& takes = lane.takes();
    if (lane.find(ref) == nullptr || takes.back().id == ref) return false;   // none, or the newest already
    recordLocked("Bring to front");
    lane.bringToFront(ref);
    changedLocked();
    return true;
}

bool LaneKeeper::undo() {
    std::lock_guard<std::mutex> guard(lock);
    pumpLocked();
    return stepLocked(undos, redos);
}

bool LaneKeeper::redo() {
    std::lock_guard<std::mutex> guard(lock);
    pumpLocked();
    return stepLocked(redos, undos);
}

bool LaneKeeper::stepLocked(std::vector<Step>& from, std::vector<Step>& to) {
    if (from.empty()) return false;
    to.push_back({from.back().label, lane.takes()});
    lane.restore(std::move(from.back().takes));
    from.pop_back();
    changedLocked();
    return true;
}

std::vector<ClipRef> LaneKeeper::takes() const {
    std::lock_guard<std::mutex> guard(lock);
    return lane.takes();
}

void LaneKeeper::setTakes(std::vector<ClipRef> takes) {
    std::lock_guard<std::mutex> guard(lock);
    pumpLocked();   // a take that just ended is replaced along with the rest
    lane.setTakes(std::move(takes));
    undos.clear();   // a loaded state starts a new history
    redos.clear();
    engine.setEvents(flatten(lane.takes()));
    changes.fetch_add(1);
}

} // namespace umpire::plugin
