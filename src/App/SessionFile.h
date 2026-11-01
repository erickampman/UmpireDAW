#pragma once

// A session on disk (milestone 9, docs/files.md): a folder, "Name.umpire",
// holding session.json (Umpire's own: the tracks, their clip references, the
// outputs, the tempo map, the loop), each clip as a MIDI Clip File
// (clips/*.midi2, App/ClipFile) and each plugin output's state
// (plugins/track-N.state). As StageHand's rack bundles: written whole into
// "Name.umpire.tmp", then swapped in, so a crash never leaves half a session.
// No toolkit types.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "App/Session.h"

namespace umpire {

// The controller's counters (App/TrackController.h), saved so a reopened
// session never reuses an id (decisions.md, "Time, clips and pitch").
struct SessionCounters {
    TrackId   nextTrack  = 1;
    int       nextNumber = 1;   // for "Track n"
    ClipRefId nextRef    = 1;
    ClipId    nextClip   = 1;

    bool operator==(const SessionCounters&) const = default;
};

// Everything a session file holds.
struct SavedSession {
    Session                        session;
    SessionCounters                counters;
    std::map<TrackId, PluginState> pluginStates;   // by track
};

struct SessionFileResult {
    bool        ok = false;
    std::string error;   // why not, for the person saving or opening
};

// Write `saved` as the bundle `bundlePath` ("X.umpire"), replacing what's there.
SessionFileResult saveSession(const std::string& bundlePath, const SavedSession& saved);
// Read the bundle `bundlePath` into `saved`, which is untouched on failure.
SessionFileResult loadSession(const std::string& bundlePath, SavedSession& saved);

// --- paths, for the file dialogs ---------------------------------------------------

inline constexpr const char* kSessionExtension = ".umpire";

// The bundle a chosen path names ("X.umpire", "X.umpire/", or its
// session.json), or "" if it isn't one.
std::string sessionBundleOf(const std::string& chosen);
// A Save As choice as a bundle path: ".umpire" added if it's missing.
std::string saveAsPath(const std::string& chosen);

enum class SaveTarget {
    New,             // nothing there
    Current,         // the session's own bundle
    OtherSession,    // another session's bundle: replace it only if the user says so
    NotASession,     // a file or folder that isn't a session: never replaced
};
SaveTarget classifySaveTarget(const std::string& target, const std::string& current);

// "Song" for ".../Song.umpire".
std::string sessionNameOf(const std::string& bundlePath);

} // namespace umpire
