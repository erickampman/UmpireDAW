#include "Plugin/LaneKeeper.h"

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
        if (!lane.addTake(take.start, std::move(take.clip))) continue;   // nothing in it
        added = true;
        if (onTake) onTake(lane.takes().back());
    }
    if (added) {
        engine.setEvents(flatten(lane.takes()));
        dirty.store(true);
    } else {
        engine.collectGarbage();
    }
    return added;
}

std::vector<ClipRef> LaneKeeper::takes() const {
    std::lock_guard<std::mutex> guard(lock);
    return lane.takes();
}

void LaneKeeper::setTakes(std::vector<ClipRef> takes) {
    std::lock_guard<std::mutex> guard(lock);
    pumpLocked();   // a take that just ended is replaced along with the rest
    lane.setTakes(std::move(takes));
    engine.setEvents(flatten(lane.takes()));
}

} // namespace umpire::plugin
