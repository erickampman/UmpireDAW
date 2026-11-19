#pragma once

// The lane thread (milestone P2, docs/plugin-version.md, "P2"): collects
// finished takes from the engine, adds them to the lane, and gives the
// engine what to play, every 10 ms, so a take lands whether or not the host
// gives the plugin its main thread (Ardour gives it none until the plugin's
// window has opened). The main thread reaches the lane only through this,
// under its lock; the audio thread never does.
//
// P3: the editor's edits (a take's notes, deleting a take, bringing one to
// the front) come in here too, on the main thread, and so does the plugin's
// own undo (docs/plugin-version.md, "P3"): each change to the lane, a take
// recorded included, is a step, the lane as it was kept (clips are shared
// and never change, so a step costs a list of references). Every change
// gives the engine new play data through its queue, as a take does.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "App/ClipEdit.h"
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
    // Replaces the lane (a loaded state) and gives the engine what to play
    // (Lane::setTakes: names kept, "Take n" from `nextNumber`).
    void setTakes(std::vector<ClipRef> takes, int nextNumber = 0);
    int  nextTakeNumber() const;

    // The lane changed since the last call, a take or an edit (for the
    // host's mark_dirty, which is the main thread's).
    bool takeDirty() { return dirty.exchange(false); }

    // --- P3: the editor (any thread but the audio thread; the main thread) ---

    static constexpr std::size_t kUndoLimit = 200;

    // Bumped by every change to the lane: an editor that showed `version()`
    // needn't look again.
    std::uint64_t version() const { return changes.load(); }
    struct View {
        std::uint64_t        version = 0;
        std::vector<ClipRef> takes;   // oldest first
        std::string          undoLabel, redoLabel;   // "" for none
    };
    View view() const;

    // The take's notes edited (App/ClipEdit.h), one undo step named `label`.
    // A change or a new note may start before the take (a negative tick: the
    // grid, in song time, reaches back to the bar the take starts in); the
    // take then starts that much earlier. No clip if nothing changed or
    // there's no such take.
    struct Edited {
        EditedClip edit;
        Tick       start = 0;   // the take's start now
    };
    Edited editTake(ClipRefId ref, const std::string& label, const std::vector<NoteChange>& noteChanges,
                    const std::vector<NewNote>& added);
    bool deleteTake(ClipRefId ref);     // "Delete take"
    bool bringToFront(ClipRefId ref);   // "Bring to front": heard where it overlaps

    // --- P4: names, scales, files, and steps outside the lane ---

    // "Rename take": false if there's no such take, the name is empty or
    // it's the same.
    bool renameTake(ClipRefId ref, const std::string& name);
    // "Set scale": the take's scale set by hand (ScaleFit::Chosen), clamped
    // to the EDOs the grid offers.
    bool setTakeScale(ClipRefId ref, const Scale& scale);
    // "Import clip": a clip from a MIDI Clip File, the newest take, from
    // `start` (Lane::addClip). Its id, or 0 if it has no events.
    ClipRefId importClip(Tick start, Clip clip);

    // A change outside the lane that the plugin's undo covers (the synth
    // changed). swap() puts back what the step holds and returns the step
    // that puts back what it replaced, or null if it couldn't now (nothing
    // changed: the step stays where it was, to try again). Called on the
    // main thread from undo() and redo(), without the keeper's lock.
    class OtherStep {
    public:
        virtual ~OtherStep()                      = default;
        virtual std::shared_ptr<OtherStep> swap() = 0;
    };
    // After such a change, `undo` the step back.
    void recordOther(std::string label, std::shared_ptr<OtherStep> undo);

    bool undo();
    bool redo();

private:
    struct Step {
        std::string                label;
        std::vector<ClipRef>       takes;   // the lane on the other side of the step
        std::shared_ptr<OtherStep> other;   // set: not the lane's (takes unused)
    };

    bool pumpLocked();
    void run();
    // Before a change named `label`: the lane as it is, for undo.
    void recordLocked(std::string label);
    // After a change: what plays, the editor's version, the host's dirty flag.
    void changedLocked();
    bool stepLocked(std::vector<Step>& from, std::vector<Step>& to);
    bool step(bool back);   // undo (true) or redo

    PluginEngine&           engine;
    TakeAdded               onTake;
    mutable std::mutex      lock;
    Lane                    lane;
    std::atomic<bool>       dirty{false};
    std::atomic<std::uint64_t> changes{0};
    std::vector<Step>       undos, redos;
    // Counted under the lock, so a step outside the lane, swapped with the
    // lock let go of, knows what happened meanwhile: steps made (a take
    // recorded), and histories started anew (a state loaded).
    std::uint64_t           stepsMade = 0, historiesCleared = 0;

    std::thread             thread;
    std::mutex              wakeLock;
    std::condition_variable wake;
    bool                    quit = false;
};

} // namespace umpire::plugin
