# Changelog

All notable changes to Umpire's plugin. Versions follow the git tags; the format is
[Keep a Changelog](https://keepachangelog.com/). The `umpire-daw` app in this repo isn't
released, so its changes aren't listed here.

## [Unreleased]

No release is planned: the plugin is built from source only, and isn't in the Unlikelyware
previews.

### Added

- Umpire as an instrument plugin: it records, edits and plays MIDI 2.0 inside a host
  (REAPER, Bitwig, Ardour), keeping per-note data and exact EDO pitch the host would round
  away. The notes never pass through the host: Umpire opens the MIDI 2.0 input itself
  (a device, or KeyboardEDO), hosts the synth itself, and gives the host audio.
- Recording follows the host's transport: a take records while the host records and
  Umpire's Record is on; each take is kept, the newest heard where takes overlap, and a jump
  while recording starts a new take (loop recording). Playback chases notes already sounding
  at the host's position, Pitch 7.25 included.
- The editor in the host's plugin window: the lane of takes in the host's bars, the
  playhead, and the selected take's grid of EDO steps, with its own undo. The hosted synth's
  editor opens in a window of its own.
- The takes and the hosted synth's state are saved in the host's project.
- Help: the **?** at the window's top right opens a help page in a window of its own.
- CLAP, for Linux (x86-64) and macOS 26 or later (Apple Silicon).
