# Umpire milestones

## Tests
- all: ninja -C build && ctest --test-dir build --output-on-failure
- self-test: build/umpire-daw --self-test DIR (opens two windows; silent; don't type into them)

## Toward a first useful version

Record MIDI 2.0 from KeyEDO or hardware, edit it in a piano roll that keeps
per-note data, play it back to plugins or hardware, and save it. One linear
timeline. Linux first (decisions.md, "Scope").

1. [x] DGL + Dear ImGui spike (ui-spike.md): on-demand redraw, vsync,
   rescale, NanoVG canvas — done 2026-10-07
2. [x] uw-core: shared UMP, plugin seam, CLAP host, JACK/ALSA and
   CoreAudio/CoreMIDI — done 2026-10-07
3. [x] DPF fork (erickampman/DPF + pugl): upstream patches, vsync opt-in,
   per-patch README — done 2026-10-14
4. [x] Umpire skeleton (skeleton.md): CMake against uw-core and DPF fork,
   main window, view-model rule, house look and feel (KeyboardEDO's
   Widgets, DejaVu Sans, colours provisional) — done on the Mac and Linux
   2026-10-23
5. [x] Track view and MIDI in (track-view.md): track rows (input,
   channels/groups, arm, mute/solo, output, activity), lanes, ALSA UMP
   input, monitor — done on the Mac and Linux 2026-10-23
6. [x] Transport, clip model and recording (recording.md): play/stop/
   record/tempo, clip model (UMP ticks, per-note data, per-key Pitch 7.25
   with no note, Per-Note Management Reset kept), undo, session input,
   sparse rows, armed tracks record clips on lanes; double-click opens the
   grid (EDO rows) — built and self-tested on Linux 2026-10-23; the Mac
   build and recording real MIDI by hand are the author's
7. [x] Playback to MIDI outputs (uw-core, then Umpire; playback.md): ALSA
   UMP output, track output to a device, scheduler, loop/locate; chase
   per-key Pitch 7.25 on play, seek and loop wrap (the synths keep it as a
   tuning table) — built and self-tested on Linux 2026-10-29, uw-core
   9f4ee51 (scheduled output); listening to it, and the Mac build, are the
   author's
8. [x] Hosting CLAP instruments (Umpire; docs/plugins.md): track output to
   a plugin, JACK (Linux) or CoreAudio (macOS) audio via uw-core,
   sample-accurate timing, offline-capable engine -- built and self-tested
   on the Mac 2026-10-30; built and tested on Linux (Umpire's and uw-core's
   tests) and heard with PlainOldSynth 2026-11-01
9. [x] Files (docs/files.md): MIDI Clip Files (M2-116-U), session JSON,
   open/save dialogs; a session is a `.umpire` folder, plugin states
   included -- built and tested on Linux 2026-11-01; checking the menu, the
   prompt and a reopened session by ear, and the Mac build (whose dialogs
   don't work yet), are the author's
10. [ ] Clip editor (piano roll): select, move, resize, delete, velocity,
    undo; Pitch 7.9 on EDO grid
11. [x] Plugin editor windows (uw-core, then Umpire; docs/plugin-editors.md):
    `EditorWindow` moved from StageHand into uw-core (`uw::editor-window`,
    X11 and Cocoa), opened from a track's row -- built and self-tested on
    the Mac 2026-10-30, done out of order (before 9 and 10) at the author's
    request; not yet built on Linux, and opening a real plugin's editor is
    the author's
12. [ ] UMP groups and arrangement (Umpire): tracks over group ranges,
    clip references with transforms
13. [ ] First useful version (Umpire): record from KeyEDO, edit, play to
    plugin and hardware, save/reopen

## Proposed: plugin routing (not yet decided)

Proposed 2026-10-30 (docs/plugin-routing.md; decisions.md, "Plugin
routing"): the session as a graph with one cable per input, MIDI and audio
mixers, and the track's plugin stack. The author reviews the mockups
first. If accepted, these get numbers, probably after 9 and 10; each is
about a milestone:

- R1. [ ] The graph (uw-core): nodes, typed ports, cables, one cable per
  input, loops refused, edits as commands, dependency order; headless tests
- R2. [ ] The engine on the graph (uw-core, then Umpire): a buffer per
  node, the order on the audio thread, note effects' events passed on
  within the block; today's routing rebuilt on it, sounding the same
- R3. [ ] The MIDI mixer and the track head: same-key policies (Merge,
  Retrigger, First wins, Louder wins), replacing `Player`'s silent merge of
  live playing and clips
- R4. [ ] The stack (Umpire): the track's output control becomes its
  stack; collapsed and expanded, note effects and audio effects, the row
  and lane growing
- R5. [ ] Side inputs and the audio mixer: sidechain pickers in slots
  (audio and notes), mixer slots, a second cable inserting a mixer
- R6. [ ] The patch view (Umpire): the popover, for routing a stack can't
  draw

Mixer strips stay with milestone 14, whose audio strips would be this
proposal's audio mixer node.

## Proposed: the tempo map and stem export (not yet scheduled)

Proposed 2026-10-30 (docs/stem-export.md; decisions.md, "Stem export"):
other DAWs finish what Umpire leaves out, from its stems, and need its
tempo map to line up with them.

- T1. [ ] The tempo map: tempo changes (sudden or ramped) and meters at
  ticks, used by the scheduler, recording, loop and locate, the ruler and
  the render; the session file already stores tempos and meters as lists
  (docs/files.md), so it needs no format change
- S1. [ ] Stem export, after milestone 14: one render pass tapping every
  track, a WAV per track (same length, from zero, `bext` time stamps), the
  tempo map as a tempo-only Standard MIDI File, a REAPER `.RPP`, a
  manifest and a reference mix; stable names, export in place, only
  changed tracks re-rendered (milestone 15's hash)
- S2. [ ] Print points and stem groups, with plugin routing's stack and
  mixers (R4, R5): wet, dry, both, or through a chosen slot
- Later: the tempo map as MIDI 2.0 Flex Data (or in a MIDI Clip File),
  once another DAW imports it

## After the first useful version: audio

Decided 2026-10-14 (decisions.md, "Audio: printing, mixdown, playback").
Plugin tracks come first; hardware comes last.

14. [ ] Mixer and offline render: per-track gain/pan/mute/solo, master bus,
    render loop; render and print start from reset keys (Per-Note
    Management Reset), not whatever played last
15. [ ] Print plugin track: render to WAV on command, staleness hash, peak
    cache
16. [ ] Audio track view: MIDI and waveform views stacked, swap front/back
17. [ ] Audio playback and mixdown: play printed audio, mix to file, play
    mix in app
18. [ ] Print hardware track: JACK capture, record with latency
    compensation

## Outside Umpire (related)
- [x] StageHand switches to uw-core (merged 2026-10-08)
- [x] StageHand switches to DPF fork (2026-10-22, StageHand b65ba55)
- [ ] Plugins move onto DPF fork and use uw::ump (plugins-roadmap.md)
- [ ] StageHand's routing moves onto uw-core's graph, if plugin routing
  (above) is accepted (plugin-routing.md, "Shared with StageHand")
