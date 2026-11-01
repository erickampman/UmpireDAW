#pragma once

// Undo and redo for the session (decisions.md, "The UI direction": one undo
// for everything). Each command keeps the session as it was before; clips are
// shared and never change, so a snapshot copies the tracks and their clip
// references, not the music.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "App/Session.h"

namespace umpire {

class History {
public:
    static constexpr std::size_t kLimit = 1000;   // commands kept

    // A command named `label` changed the session from `before`.
    void record(std::string label, Session before) {
        undos.push_back({std::move(label), std::move(before), current});
        if (undos.size() > kLimit) undos.erase(undos.begin());
        redos.clear();
        current = ++made;
    }

    // Each swaps `session` with the state on the other side of the last
    // command; false when there's none.
    bool undo(Session& session) { return step(undos, redos, session); }
    bool redo(Session& session) { return step(redos, undos, session); }

    // Which state the session is in: each command makes a new one, and undo
    // and redo go back to theirs. Saving notes it, so undoing to what was
    // saved means nothing is unsaved (milestone 9).
    std::uint64_t state() const noexcept { return current; }

    // What Edit > Undo and Redo would do ("" for nothing).
    std::string undoLabel() const { return undos.empty() ? std::string() : undos.back().label; }
    std::string redoLabel() const { return redos.empty() ? std::string() : redos.back().label; }

private:
    struct Entry {
        std::string   label;
        Session       session;
        std::uint64_t state = 0;
    };

    bool step(std::vector<Entry>& from, std::vector<Entry>& to, Session& session) {
        if (from.empty()) return false;
        to.push_back({from.back().label, std::move(session), current});
        session = std::move(from.back().session);
        current = from.back().state;
        from.pop_back();
        return true;
    }

    std::vector<Entry> undos, redos;
    std::uint64_t      current = 0, made = 0;
};

} // namespace umpire
