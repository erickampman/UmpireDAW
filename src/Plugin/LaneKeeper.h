#pragma once

// The lane thread (milestone P2, docs/plugin-version.md, "P2"): collects
// finished takes from the engine, adds them to the lane, and gives the
// engine what to play, every 10 ms, so a take lands whether or not the host
// gives the plugin its main thread (Ardour gives it none until the plugin's
// window has opened). The main thread reaches the lane only through this,
// under its lock; the audio thread never does.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "Plugin/Lane.h"
#include "Plugin/PluginEngine.h"

namespace umpire::plugin {

class LaneKeeper {
public:
    // Called on whichever thread collected it (the lane thread, or the main
    // thread in pump()), with the lock held: for the log. Don't call back in.
    using TakeAdded = std::function<void(const ClipRef& take)>;

    explicit LaneKeeper(PluginEngine& engine, TakeAdded onTake = {});
    ~LaneKeeper();   // stops the thread

    LaneKeeper(const LaneKeeper&)            = delete;
    LaneKeeper& operator=(const LaneKeeper&) = delete;

    // The lane thread. Without it (the tests), call pump() instead.
    void start();
    void stop();

    // Collects finished takes into the lane and, if it changed, gives the
    // engine what to play now; frees what the audio thread let go of. True
    // if a take was added. Any thread but the audio thread.
    bool pump();

    // A copy of the lane's takes, oldest first (for the state).
    std::vector<ClipRef> takes() const;
    // Replaces the lane (a loaded state) and gives the engine what to play.
    void setTakes(std::vector<ClipRef> takes);

    // A take was added since the last call (for the host's mark_dirty, which
    // is the main thread's).
    bool takeDirty() { return dirty.exchange(false); }

private:
    bool pumpLocked();
    void run();

    PluginEngine&           engine;
    TakeAdded               onTake;
    mutable std::mutex      lock;
    Lane                    lane;
    std::atomic<bool>       dirty{false};

    std::thread             thread;
    std::mutex              wakeLock;
    std::condition_variable wake;
    bool                    quit = false;
};

} // namespace umpire::plugin
