# Umpire milestones

## Tests
- all: ninja -C build && ctest --test-dir build --output-on-failure
- self-test: build/umpire-daw --self-test DIR (opens two windows; silent; don't type into them).
  On the Mac its grid-window input checks fail, as before P3 (plugin-version.md, "P3")
- plugin window (Mac): ninja -C build umpire-plugin-window-check, then
  UMPIRE_PLUGIN_CONF=build/test-plugin.conf UMPIRE_PLUGIN_LOG=none UMPIRE_PLUGIN_CAPTURE=DIR
  build/umpire-plugin-window-check build/Umpire.clap DIR (opens a window; silent).
  Its clicks reach the window only while nothing else has the keys: run it
  with the Mac otherwise idle (2026-11-19, P4). With a second screen of
  another scale it also moves its window there and back (2026-11-20).
  Since Z1 it zooms the grid and the lane with Command-wheel.
- AUv2 (Mac): cmake --build build --target install-auv2, then
  auval -v aumu Umpr nLKL

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
   included -- built and tested on Linux 2026-11-01, and on the Mac the same
   day, with AppKit's panels as its dialogs; checking the menu, the prompt,
   the Mac's panels and a reopened session by ear are the author's
10. [x] Clip editor (piano roll; docs/clip-editor.md): select, move,
    resize, delete, velocity, undo; Pitch 7.9 on EDO grid; per-note data
    kept; each edit a command making a new clip -- built and tested on
    Linux 2026-11-01; checking it by eye and by ear, and the Mac build, are
    the author's
11. [x] Plugin editor windows (uw-core, then Umpire; docs/plugin-editors.md):
    `EditorWindow` moved from StageHand into uw-core (`uw::editor-window`,
    X11 and Cocoa), opened from a track's row -- built and self-tested on
    the Mac 2026-10-30, done out of order (before 9 and 10) at the author's
    request; not yet built on Linux, and opening a real plugin's editor is
    the author's
12. [ ] UMP groups and arrangement (Umpire): tracks over group ranges,
    clip references with transforms -- *a question for P2 now (below)*
13. [ ] ~~First useful version (Umpire): record from KeyEDO, edit, play to
    plugin and hardware, save/reopen~~ -- *replaced by the plugin, P2 to P5*

**Decided 2026-11-18: Umpire as a plugin** (decisions.md; below). The
standalone milestones after 11 are replaced: 13 to 18, T1, S1, S2 and R1 to
R6 won't be built. `umpire-daw` stays as it is, the test bed for the clip
model and the grid. The sections below are kept as the record.

## Replaced: plugin routing (proposed, not built)

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

## Replaced: the tempo map and stem export (proposed, not built)

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

## Umpire as a plugin (decided 2026-11-18)

Proposed and decided 2026-11-18 (docs/plugin-version.md; decisions.md,
"Umpire as a plugin"): Umpire as a CLAP instrument in REAPER, Bitwig or
Ardour, opening the MIDI 2.0 input and hosting the synth itself, so the
notes never pass through the host. It replaces milestones 13 to 18, T1, S1,
S2 and R1 to R6.

- P1. [x] The spike: one input, one hosted synth, one clip recorded with
  the host and played back from its position, saved in the state; checked
  in REAPER and Bitwig, and in Ardour (open source) through the VST3 build,
  on the Mac and Linux. *Built 2026-11-18 on the Mac (`Umpire.clap`,
  `Umpire.vst3`; plugin-version.md, "The spike, built"). Checked headless:
  the tests, clap-validator, Steinberg's VST3 validator, and renders through
  PlainOldSynth (per-note pitch whole, onsets on the beat at two tempos,
  offline = real time, state, two instances). Checked by hand (the
  author's) in REAPER and Bitwig on the Mac, and on Linux (built there the
  same day) in REAPER, Bitwig and Ardour 8.4: Ardour's checks 1 to 7 pass,
  with timers only once the plugin's window has opened (plugin-version.md,
  "In hosts"; release notes there). Done 2026-11-18. Not done, and not
  needed to go on: REAPER's check 6 (Bitwig and Ardour passed it); Ardour
  on the Mac (the author's is Intel-only).*
- P2. [ ] Recording and playback on the clip model, driven by the host's
  position; the chase on jumps; latency reported. *Built 2026-11-18 on the
  Mac (plugin-version.md, "P2"): every take kept on the plugin's lane, the
  newest heard where takes overlap; a jump while recording starts a new
  take (loop recording); takes collected on a lane thread of the plugin's
  own, so Ardour no longer needs the window opened for them; tempo changes
  inside a block; the synth's latency reported (uw-core df496de); state
  version 2. Checked headless (10 of 10, clap-validator). Added
  2026-11-19: the arm, a Record parameter (off by default), so a take
  records only while the host records and Record is on; state version 3;
  VST3 validator 47 of 47. PlainOldSynth now keeps Pitch 7.25 per key, so
  the chase works with it. The checks in hosts (the author's, 8) pass on
  Linux in REAPER and Ardour 9 (2026-11-20; plugin-version.md, "P2", "On
  Linux"). Bitwig on Linux, 2026-11-24 (the author): takes from bars
  other than the first, save and reopen, loop recording, a tempo change
  while playing (from the transport; Bitwig's tempo lane too obscure to
  find), recording over a take, the arm (Record off, no take; Record
  automated) and two instances on one input with only one armed pass,
  and latency (check 6) has nothing to show, as
  elsewhere; PlainOldSynth's tuning table (check 8) passes both ways.
  Left: the rest of Bitwig, and the Mac (REAPER
  passed check 7 there).*
- P3. [ ] The editor: a lane in time with the host, and the grid. *Built
  2026-11-19 on the Mac (plugin-version.md, "P3"; decisions.md, "The
  plugin's window"; the design agreed with the author first): the host's
  plugin window is Umpire's, with a Record button on the Record parameter
  (a gesture to the host), the lane of takes in the host's bars (older
  takes' covered parts as a strip; Bring take to front; Delete take), the
  playhead, and the selected take's grid in song time (the app's
  `GridCanvas` and `GridPanels`, shared through `umpire-grid-ui`); edits go
  through the lane (`LaneKeeper::editTake`) with the plugin's own undo,
  takes recorded included; the synth's editor in a window of its own.
  Checked headless (10 of 10, testEditor), with tools/PluginWindowCheck.mm
  (the window opened and clicked in a bare Cocoa host), clap-validator (30,
  the 2 expected failures) and the VST3 validator (47 of 47). The checks
  in hosts (the author's, 8) pass in REAPER on Linux (2026-11-20;
  plugin-version.md, "P3"). Bitwig on Linux, 2026-11-24 (the author):
  the window, editing and undo, Bring take to front, Delete take,
  Record's automation and two instances work. In Bitwig on Linux all eight pass.
  Ardour 9 on Linux (VST3), 2026-11-24: all eight pass. Left: the Mac.*
- P4. [ ] State and files: clips as MIDI Clip Files in the state, the
  synth's state, clip export. *Built 2026-11-19 on the Mac
  (plugin-version.md, "P4"; decisions.md, "State and files"; the scope
  agreed with the author first): takes keep their names and scales (the
  grid's name field and tuning are the take's, "Rename take" and "Set
  scale" undo steps); Export take... and Import clip... (a .midi2, at the
  playhead) through the app's file dialogs; the synth chosen per instance
  from a menu beside its button, swapped while the host plays (the audio
  thread lets go of the old one, lock-free), "Change synth" undoable with
  its patch; the input chosen from a menu beside its name; both kept in
  the state, and a synth or input that isn't here kept as it was and shown
  as missing; state version 4 (1 to 3 still load). Checked headless (10 of
  10), clap-validator (30, the 2 expected failures) and the VST3 validator
  (47 of 47). tools/PluginWindowCheck.mm couldn't click into its window
  here (P3's build failed the same way the same day: the Mac was in use,
  so the check's window couldn't take the keys); to run again on an idle
  Mac. Builds on Linux. Ardour 9 on Linux (VST3), 2026-11-24 (the
  author): all eight checks pass; zenity 4's export dialog starts with no
  name and asks "Replace?" as Umpire does, for the release notes. Left:
  the checks in REAPER and Bitwig, and the Mac.*
- AU. [ ] The AUv2, for Logic and MainStage (not a milestone of the
  proposal; asked for 2026-11-20, plugin-version.md, "AUv2 for Logic").
  *Built 2026-11-20 on the Mac: `Umpire.component` (aumu, "Umpr" under
  "nLKL") through clap-wrapper, whose fork (01f838b) now passes offline
  render, the cycle's start and beat 0; auval passes. Record modes by the
  host's name, since no host API says whether a host records: "host" (its
  record button, as before), "play" in Logic and GarageBand (they never say
  they record, so a take starts when the host plays with Record on, and the
  lane plays on meanwhile), "off" in MainStage and StageHand (a player;
  Record shown disabled); `record =` in plugin.conf overrides. The window on
  screens of another scale: ImGui rescales with the window, and the view's
  size is checked each tick (it had gone blank in Logic when dragged from
  the Retina screen to a 1x monitor). Checked by the author in Logic Pro 13
  (Pitch 7.25, recording on play, the window between screens) and MainStage
  5 (Pitch 7.25). The count-in: nothing is recorded before Logic itself
  records (a count-in found from where Logic stood; never before bar 1), and
  a take starts on its first note's beat (Take 1 had begun at beat -4).
  Untried in Logic: a count-in before a later bar. Left: in Logic, cycle recording, live input on a track that isn't selected, a bounce; in
  MainStage, record off.*
- P5. [ ] Host checks: REAPER and Bitwig on both platforms, Ardour on Linux
  (and Logic and MainStage on the Mac, for the AUv2). The other open-source
  hosts (Qtractor, Carla, Zrythm) aren't checked (the author, 2026-11-24).
  Ardour on the Mac isn't required
  (2026-11-24): its free download is a demo that goes silent after about
  ten minutes, and the full build is paid
- PR. [ ] REAPER's pre-roll: what a take keeps from before REAPER records.
  Found by the author, 2026-11-20: with pre-roll on, Take 2 began at bar -1
  (the log: "from beat -8.02 to beat 23.34, first note-on at beat -1.00"),
  since REAPER says it records through the pre-roll and never sets
  `CLAP_TRANSPORT_IS_WITHIN_PRE_ROLL` (the log's transport line: "playing,
  recording", no "pre-roll"), and Umpire honours only that flag. Undecided:
  what, if anything, a take keeps from before where the host stood. Not
  necessarily nothing: Digital Performer had a mode that kept notes played
  just before 1:1:00 when they were only a little early, and a note played
  a little early in the pre-roll may be meant for the downbeat. Against it:
  for Logic the author found recording earlier than Logic does "too
  confusing" (an earlier try kept count-in notes, and was replaced). Options: drop everything before (one
  way: Logic's count-in rule, `recordStartOnPlay`, for hosts that say they
  record too, when recording starts with playback: from earlier than where
  the host stood, by at most six bars, record from where it stood), keep
  only notes a little early (how early, and whether they move to the
  downbeat), or keep the pre-roll as now. Whatever is chosen, Logic's
  count-in may want the same. Punching in while playing is unchanged. If
  the cut is found from where the host stood, the whole-beat test would go
  for these hosts: REAPER's -8.02 is 0.02 beats off (about 10 ms at 120 bpm, likely its
  output latency), outside the 1/64-beat tolerance. To check: whether REAPER
  runs the plugin while stopped (otherwise only "never before bar 1"
  applies), and Bitwig's pre-roll. The other way, REAPER's own CLAP
  extension reading its pre-roll setting, works only in REAPER.
- HE. [ ] Hearing notes while editing: a note sounds through the take's
  synth when it's clicked, as it's dragged (at each new pitch), and when
  it's added, in the plugin's grid and the app's. Asked for by the author,
  2026-11-20, during P3's checks in REAPER, as **required** (the grid made
  no sound while editing; edits were heard only on playback). *Decided
  with the author 2026-11-22: a note sounds while the mouse button is held
  (a press on a note, or on empty grid where a note would be added), and a
  drag to another row ends it and sounds the new pitch; at the note's own
  velocity (a new note's, 100); whether or not the host plays; at its
  starting pitch (its Pitch 7.9, or its row's once moved), bends within it
  not played; Edit > Hear notes while editing, on by default, kept in the
  plugin's state (flags bit 2, no new version) and the app's session
  (`view.hearNotes`). Built 2026-11-22 on the Mac (clip-editor.md,
  "Hearing notes"): `GridModel::hear`, on group 1, channel 16, so it never
  ends a note the clip plays; in the plugin through `PluginEngine::hear`,
  played at the next block's start and never recorded (an offline render
  drops it); in the app through `TrackController::hear`, the clip's
  track's output, as live input (Player::thru). Checked headless: 11 of
  11, testHearing (the note, its channel and velocity, a drag's new pitch,
  heard while recording but not in the take, an EDO row's Pitch 7.9, the
  switch, a render, the state's bit) and test-files (the session's
  switch); clap-validator 32 passed, 0 failed, 12 skipped; auval passes.
  Heard by hand in Bitwig on Linux (CLAP), 2026-11-24 (the author): notes
  sound while editing in the grid. Heard in Ardour 9 on Linux (VST3) too,
  2026-11-24. Left: REAPER, the app, and the Mac.*
- NE. [ ] Note effects in front of the synth: CLAP note effects (Retune,
  NoteGran, EDOSpread12, others') hosted inside the plugin, in order, between
  the input and the synth, since the host's own note effects never reach
  Umpire (plugin-version.md, "What's given up"). Added 2026-11-22 at the
  author's request. The UI follows StageHand's MIDI FX menu (its help,
  "MIDI Effects"): a menu in the header beside the synth's button, showing
  the first effect's name ("+1" for each more) or "MIDI FX" when there are
  none; **Add a MIDI effect...** lists the installed note effects, a new one
  going after the others; for each effect **Editor**, **Bypass**, **Move
  up**, **Move down** and **Remove**; with one effect, **E** beside the menu
  opens its editor; an effect that isn't installed shows as missing, is kept
  and saved, and is passed by until it's back. The effects and their states
  go in the plugin's state (a new version); adding, removing, moving and
  bypassing are undo steps, as "Change synth". To decide with the author
  before it's built: whether a take records what's played (before the
  effects, so they act again on playback, as StageHand's rows and a DAW's
  MIDI FX do) or what they put out; whether the grid's HE notes go through
  them; and how much of StageHand's note-effect hosting moves into uw-core
  to be shared. *Decided with the author 2026-11-22: a take records what's
  played, before the effects; the grid's HE notes go through them; the
  chain is a new uw-core `NoteEffectChain` (uw::note-effects), Umpire its
  first user, StageHand moving onto it later. Built 2026-11-22 on the Mac
  (plugin-version.md, "NE"): the MIDI FX menu before the synth, with E;
  each effect's editor in a window of its own; edits as undo steps; the
  synth and its effects swapped as one slot, kept plugins staying active,
  All Notes Off to a kept synth; the state's version 6. Checked headless:
  11 of 11 (testEffects, testStateV6; with the real Retune in front of
  PlainOldSynth in test-plugin-render), uw-core's test-note-effect-chain;
  clap-validator 32 passed, 0 failed, 12 skipped; auval passes; the window
  check passes. Adding, removing and bypassing effects work in a host (the
  author, 2026-11-22). Left: Linux.*

## Zoom

Asked for 2026-11-01: Umpire had no zoom; the track view's beat
(`TrackCanvas::kBeatW`) and the grid's step and row (`GridCanvas::kStepW`,
`kRowH`) were fixed.

- Z1. [x] Horizontal zoom in the plugin's lane and grid and the app's track
  view and grid, about the pointer, as StageHand's sequence editor
  (Cmd-wheel on the Mac, Ctrl-wheel on Linux); a two-finger pinch on a Mac
  trackpad zooms too, through the pugl fork's `magnifyWithEvent:`. The lane
  and the grid zoom separately, from 1/8x to 8x; the plugin's state
  (version 5) keeps both without marking the project changed. *Built
  2026-11-20 on the Mac* (plugin-version.md, "Z1"; decisions.md, "Zoom").
  Zoom and scrolling work in Logic, REAPER and Bitwig (the author,
  2026-11-20). The grid now opens with the take's first note mid-height.
  The app's session keeps the track view's and the grid's zoom since
  2026-11-20 (files.md, "`view`"), checked on Linux.
  Left: Linux (Ctrl-wheel)
- Later: the zoom levels of ui-direction.md ("Zoom replaces opening
  things": Sections, Bars, Notes, Expression), editing notes in the lane

## Replaced: after the first useful version, audio (not built)

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
- [ ] ~~StageHand's routing moves onto uw-core's graph, if plugin routing
  (above) is accepted~~ (plugin routing is replaced; StageHand may still
  want the graph on its own)
