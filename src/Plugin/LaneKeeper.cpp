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
    undos.push_back({std::move(label), lane.takes(), nullptr});
    if (undos.size() > kUndoLimit) undos.erase(undos.begin());
    redos.clear();
    ++stepsMade;
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

bool LaneKeeper::renameTake(ClipRefId ref, const std::string& name) {
    std::lock_guard<std::mutex> guard(lock);
    pumpLocked();
    const ClipRef* take = lane.find(ref);
    if (take == nullptr || !take->clip || name.empty() || take->clip->name == name) return false;
    auto copy  = std::make_shared<Clip>(*take->clip);   // the same music: the same id
    copy->name = name;
    recordLocked("Rename take");
    lane.replace(ref, take->start, std::move(copy));
    changedLocked();
    return true;
}

bool LaneKeeper::setTakeScale(ClipRefId ref, const Scale& scale) {
    std::lock_guard<std::mutex> guard(lock);
    pumpLocked();
    const ClipRef* take = lane.find(ref);
    if (take == nullptr || !take->clip) return false;
    const Scale clamped{std::clamp(scale.edo, Scale::kMinEdo, Scale::kMaxEdo), std::clamp(scale.referenceNote, 0, 127)};
    if (take->clip->scale == clamped && take->clip->scaleFit == ScaleFit::Chosen) return false;
    auto copy      = std::make_shared<Clip>(*take->clip);
    copy->scale    = clamped;
    copy->scaleFit = ScaleFit::Chosen;
    recordLocked("Set scale");
    lane.replace(ref, take->start, std::move(copy));
    changedLocked();
    return true;
}

ClipRefId LaneKeeper::importClip(Tick start, Clip clip) {
    std::lock_guard<std::mutex> guard(lock);
    pumpLocked();
    if (clip.events.empty()) return 0;
    recordLocked("Import clip");
    const ClipRefId ref = lane.addClip(start, std::move(clip));
    changedLocked();
    return ref;
}

void LaneKeeper::recordOther(std::string label, std::shared_ptr<OtherStep> step) {
    if (!step) return;
    std::lock_guard<std::mutex> guard(lock);
    undos.push_back({std::move(label), {}, std::move(step)});
    if (undos.size() > kUndoLimit) undos.erase(undos.begin());
    redos.clear();
    ++stepsMade;
    dirty.store(true);
    changes.fetch_add(1);
}

bool LaneKeeper::undo() { return step(true); }
bool LaneKeeper::redo() { return step(false); }

// A lane step is swapped under the lock. A step outside the lane (the synth)
// is swapped with the lock let go of, since it calls the host, which might
// call back (a state save takes the lock). Meanwhile the lane thread may make
// a step (a take recorded), which clears the redos, or a state may load,
// which clears both: what's put back afterwards keeps to that.
bool LaneKeeper::step(bool back) {
    std::unique_lock<std::mutex> guard(lock);
    pumpLocked();
    std::vector<Step>& from = back ? undos : redos;
    std::vector<Step>& to   = back ? redos : undos;
    if (from.empty() || !from.back().other) return stepLocked(from, to);
    Step taken = std::move(from.back());
    from.pop_back();
    const std::uint64_t made = stepsMade, cleared = historiesCleared;
    guard.unlock();
    std::shared_ptr<OtherStep> reverse = taken.other->swap();
    guard.lock();
    const bool reset    = historiesCleared != cleared;
    const bool newSteps = stepsMade != made;   // the redos were cleared
    const auto keep     = [](std::vector<Step>& in, Step s) {
        in.push_back(std::move(s));
        if (in.size() > kUndoLimit) in.erase(in.begin());
    };
    if (!reverse) {   // it couldn't now: nothing changed, the step stays
        if (!reset && (back || !newSteps)) keep(from, std::move(taken));
        return false;
    }
    if (!reset && !(back && newSteps)) keep(to, {taken.label, {}, std::move(reverse)});
    dirty.store(true);
    changes.fetch_add(1);
    return true;
}

bool LaneKeeper::stepLocked(std::vector<Step>& from, std::vector<Step>& to) {
    if (from.empty()) return false;
    to.push_back({from.back().label, lane.takes(), nullptr});
    lane.restore(std::move(from.back().takes));
    from.pop_back();
    changedLocked();
    return true;
}

std::vector<ClipRef> LaneKeeper::takes() const {
    std::lock_guard<std::mutex> guard(lock);
    return lane.takes();
}

int LaneKeeper::nextTakeNumber() const {
    std::lock_guard<std::mutex> guard(lock);
    return lane.nextTakeNumber();
}

void LaneKeeper::setTakes(std::vector<ClipRef> takes, int nextNumber) {
    std::lock_guard<std::mutex> guard(lock);
    pumpLocked();   // a take that just ended is replaced along with the rest
    lane.setTakes(std::move(takes), nextNumber);
    undos.clear();   // a loaded state starts a new history
    redos.clear();
    ++historiesCleared;
    engine.setEvents(flatten(lane.takes()));
    changes.fetch_add(1);
}

} // namespace umpire::plugin
