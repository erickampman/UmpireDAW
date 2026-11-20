#pragma once

// The plugin's setup and saved state (milestones P1, P2 and P4, docs/plugin-version.md),
// with no CLAP or file system in it, for the tests.
//
// What a new instance opens comes from a small text file (Plugin/UmpirePlugin.cpp says where), one "key = value" a line,
// '#' for comments:
//   input = KeyboardEDO      the MIDI input to open: part of its name or id,
//                            any case; "none" for none; left out, it chooses
//   synth = PlainOldSynth    the synth to host: a .clap's name in CLAP's
//                            folders; "none" for none
//   synth-id = ...           which plugin in that .clap (left out: its first)
//   record = play            what starts a take: "host", the host's record
//                            button; "play", the host playing (with the
//                            arm); "off", nothing (a player, for live hosts);
//                            left out, by the host (recordMode)
// Since P4 the window chooses both for an instance, and its state keeps that.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "App/Scale.h"
#include "uw/midi/MidiInput.h"

namespace umpire::plugin {

struct Config {
    std::string input;                     // empty: choose
    std::string synth = "PlainOldSynth";   // "none": no synth
    std::string synthId;                   // empty: the .clap's first plugin
    std::string record;                    // "host", "play", "off"; empty: by the host
};

// What starts a take. No host API says whether a host records at all, so
// it's by the host's name, unless the setup file's `record` says:
// - Off in live hosts (MainStage, StageHand), which don't record: Umpire is a
//   MIDI 2.0 player there, its Record button disabled.
// - Play in hosts that record but never say so (Logic Pro, GarageBand: Logic
//   Pro 13 gives an Audio Unit playing, beats and tempo, never recording).
// - Host elsewhere: the host's record button, with the arm.
enum class RecordMode : std::uint8_t { Host, Play, Off };
RecordMode recordMode(const std::string& record, const std::string& hostName);
const char* recordModeName(RecordMode mode);

Config parseConfig(const std::string& text);

// The source to open, as an index into `sources`, or -1 for none. With
// `wanted`, the first whose name or id contains it (any case); without,
// KeyboardEDO's, else the first MIDI 2.0 source, else the first hardware one.
int chooseSource(const std::vector<uw::MidiSourceInfo>& sources, const std::string& wanted);

// The plugin's CLAP state: the lane's takes, each as where it starts and a
// MIDI Clip File (App/ClipFile.h) with its name and scale; the hosted synth,
// which and its own state; the arm (the Record parameter); and the input
// chosen in the window. Little-endian, strings as a size (u32) and bytes:
//   "UMPP", version (u32, 4),
//   take count (u32), then for each, oldest first: its start (i64 ticks on
//   the host's timeline), clip file size (u32) and bytes, name, its scale's
//   EDO (u32) and reference note (u32) and how it was arrived at (u32,
//   App/Scale.h's ScaleFit),
//   the next take's number (u32),
//   synth id, synth state size (u32) and bytes,
//   flags (u32; bit 0: armed; bit 1: the synth was chosen, so an empty id is
//   none),
//   the synth's .clap path, the synth's name,
//   the input: how it's chosen (u32, InputMode), its name, its id.
// Version 3 (P2, the arm) had no names, scales, next number, chosen bit,
// synth path or name, or input. Version 2 (P2) had no flags either: read
// unarmed. Version 1 (the spike, P1) had one clip file in place of the takes,
// from the song's start: it's read as one take at 0. Versions 1 to 3 name the
// synth by its id only, and a non-empty one counts as chosen.
struct SavedTake {
    std::int64_t              start = 0;   // ticks (App/Clip.h's 40320 a quarter)
    std::vector<std::uint8_t> clipFile;
    std::string               name;        // empty: given one on load ("Take n")
    Scale                     scale;
    ScaleFit                  scaleFit = ScaleFit::Default;   // only Chosen is kept; the rest is worked out again
};

// A take with only its start and clip (an older state's, or the tests').
inline SavedTake savedTake(std::int64_t start, std::vector<std::uint8_t> clipFile) {
    SavedTake take;
    take.start    = start;
    take.clipFile = std::move(clipFile);
    return take;
}

// How the input was chosen in the window (P4).
enum class InputMode : std::uint32_t {
    Setup = 0,   // the setup file's, or the automatic choice (a new instance)
    Named = 1,   // the source `name` / `id`, kept even when it isn't there
    None  = 2,   // none
};

struct InputChoice {
    InputMode   mode = InputMode::Setup;
    std::string name, id;
    bool operator==(const InputChoice&) const = default;
};

struct SavedState {
    std::vector<SavedTake>    takes;
    std::uint32_t             nextTakeNumber = 0;   // 0: one past the takes' own
    std::string               synthId;
    std::vector<std::byte>    synthState;
    bool                      armed = false;
    // P4: false is the setup file's synth (a new instance, or an older state
    // that named none); true, the synth is synthId, or none if that's empty.
    bool                      synthChosen = false;
    std::string               synthPath;   // the .clap it was loaded from
    std::string               synthName;   // to say which is missing
    InputChoice               input;
};

std::vector<std::uint8_t> encodeState(const SavedState& state);
// False, with `error` saying why, if `data` isn't such a state.
bool decodeState(const std::uint8_t* data, std::size_t size, SavedState& state, std::string& error);

} // namespace umpire::plugin
