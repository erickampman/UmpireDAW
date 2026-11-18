#pragma once

// The spike's setup and saved state (milestone P1, docs/plugin-version.md),
// with no CLAP or file system in it, for the tests.
//
// The spike has no editor of its own, so what it opens comes from a small
// text file (Plugin/UmpirePlugin.cpp says where), one "key = value" a line,
// '#' for comments:
//   input = KeyboardEDO      the MIDI input to open: part of its name or id,
//                            any case; "none" for none; left out, it chooses
//   synth = PlainOldSynth    the synth to host: a .clap's name in CLAP's
//                            folders; "none" for none
//   synth-id = ...           which plugin in that .clap (left out: its first)

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "uw/midi/MidiInput.h"

namespace umpire::plugin {

struct Config {
    std::string input;                     // empty: choose
    std::string synth = "PlainOldSynth";   // "none": no synth
    std::string synthId;                   // empty: the .clap's first plugin
};

Config parseConfig(const std::string& text);

// The source to open, as an index into `sources`, or -1 for none. With
// `wanted`, the first whose name or id contains it (any case); without,
// KeyboardEDO's, else the first MIDI 2.0 source, else the first hardware one.
int chooseSource(const std::vector<uw::MidiSourceInfo>& sources, const std::string& wanted);

// The plugin's CLAP state: the clip as a MIDI Clip File (App/ClipFile.h),
// and the hosted synth, which and its own state. Little-endian:
//   "UMPP", version (u32, 1),
//   clip file size (u32) and bytes (0: no clip),
//   synth id size (u32) and bytes, synth state size (u32) and bytes.
struct SavedState {
    std::vector<std::uint8_t> clipFile;
    std::string               synthId;
    std::vector<std::byte>    synthState;
};

std::vector<std::uint8_t> encodeState(const SavedState& state);
// False, with `error` saying why, if `data` isn't such a state.
bool decodeState(const std::uint8_t* data, std::size_t size, SavedState& state, std::string& error);

} // namespace umpire::plugin
