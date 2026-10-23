# Roadmap

Milestones toward the first useful version (decisions.md, "Scope"): record
MIDI 2.0 from KeyEDO or hardware, edit it in a piano roll that keeps per-note
data, play it back to plugins or hardware, and save it. One linear timeline.
Linux first.

Drafted 2026-10-07, after milestone 1. Each milestone is about a day, with a
fresh session, and starts by reading decisions.md, this file and the docs the
milestone names. When a milestone finishes, mark it done here and put what
was learned in the docs. Later milestones are rougher, so expect to revise
them as earlier ones teach things.

Two other repos take part (decisions.md, "Shared code" and "DPF fork"):
**uw-core** (`~/dev/dev-common/uw-core`) and the **DPF fork** (to be made,
beside it). A milestone says which repo it works in.

## 1. DGL plus Dear ImGui spike (Umpire): done 2026-10-07

[ui-spike.md](ui-spike.md).

## 2. uw-core: copy the shared code from StageHand (uw-core): done 2026-10-07

uw-core's README.md says what moved and how it changed. The changes list
there is what StageHand's switch works from. Six tests pass on Linux,
including a CLAP instrument (ModicumFM) processing into memory. The macOS
sources were copied but haven't been built.

Copy StageHand's code that both apps need into uw-core, as the layered
targets that decisions.md describes:
- `uw::ump`: `MidiMessage` and the UMP helpers. No dependencies, `-fPIC`.
- `uw::base`: clock, SPSC queue, denormals, audio buffer, time info, events.
- `uw::plugin` and `uw::clap-host`: the plugin seam and its CLAP
  implementation.
- `uw::audio` and `uw::midi`: JACK and ALSA sequencer UMP input; CoreAudio
  and CoreMIDI, copied but only built on the Mac.

Also: the namespace `uw`, StageHand's tests for these parts, an MIT licence,
and options so a plugin can build `uw::ump` alone, without JACK, ALSA or
CLAP. StageHand isn't changed: it keeps its own copy until its switch.

## 3. The DPF fork (DPF fork): done 2026-10-14

The fork is `erickampman/DPF` (`~/dev/dev-common/DPF`), with pugl in a
second fork, `erickampman/pugl` (`~/dev/dev-common/pugl`), because PMU's
macOS patches are in pugl (decisions.md, "DPF fork"). Its `README.fork.md`
lists the base (upstream DPF `4238e1c7`, pugl `5e2621d`, exactly what
StageHand's vendored copy was apart from its patches) and each local patch,
one commit each. The vsync patch is opt-in: a consumer defines
`DGL_SWAP_INTERVAL=0` on `dgl-opengl`, and Umpire will (milestone 4). Checked
on Linux: DPF's examples build, and a DGL window's swap interval is 1 without
the define and 0 with it. On macOS pugl ignores the hint.

Make the private DPF fork in `~/dev/dev-common`: upstream DPF plus
StageHand's vendored patches (PMU's and StageHand's). Add the vsync patch
(ui-spike.md, finding 6): set `PUGL_SWAP_INTERVAL` to 0 when DGL makes its
view, behind a define so each user can choose. Write down each local patch
in the fork's README, as StageHand's `README.StageHand.md` does. Build a DGL
example on Linux. The plugins' own changes come later, in the plugin roadmap.

## 4. Umpire's skeleton (Umpire): done on the Mac 2026-10-23

[skeleton.md](skeleton.md). `umpire-daw` builds over the uw-core and DPF
fork submodules, with Umpire's own ImGui widget, KeyboardEDO's house widgets
(in provisional Umpire colours) and StageHand's main loop; the spike is
deleted. Built and self-tested on the Mac, then on Linux (2026-10-23); the
hand checks and the colours are the author's (skeleton.md, "Still to do").

Umpire's own CMake project builds against uw-core and the DPF fork as
submodules, replacing `UMPIRE_DPF_DIR`. It has:
- the `umpire-daw` binary with a main loop driven like StageHand's;
- one main window: the NanoVG canvas plus **Umpire's own ImGui widget**,
  adapted from DPF-Widgets' (ui-spike.md, proposal 2), with the spike's
  routing, on-demand redraw and rescale;
- the view-model rule: no DGL or ImGui types below the drawing layer.

The spike (`spike/`) is deleted once `umpire-daw` does what it did
(decisions.md, "Milestone 4", decided 2026-10-22).

**The house look and feel, from the start** (asked for 2026-10-18):
everything should look like the plugins and KeyboardEDO where it can. That's
PlainOldSynth's glowing knobs, group boxes, captioned dropdowns and value
boxes, a linear slider in the same style, and DejaVu Sans, in Umpire's own
colours (KeyboardEDO's `docs/decisions.md`, "Look and feel"). Umpire's
panels are ImGui, as KeyboardEDO's are, so they can start from KeyboardEDO's
`src/app/Widgets.{h,cpp}` (`kedo::app::ui`: knob, slider, value box, section,
theme). The NanoVG canvas can use the plugins' NanoVG widgets
(`src/UI/Knob.hpp` and the rest). The widgets are copied into Umpire, not
shared (decisions.md, "Milestone 4"). Plugin editors
keep their own look.

**Revised 2026-10-23, after milestone 4** (decisions.md, "The track
view"): the main window is the track view, not the grid. Most MIDI arrives
by playing it in, so track rows (as StageHand's rack rows) come first, and
the grid becomes the clip editor, opened by double-clicking a clip. Tracks
moved up from milestone 11, transport and recording from 8, and the files
and the piano roll moved down.

## 5. The track view and MIDI in (Umpire): done on the Mac 2026-10-23

[track-view.md](track-view.md). Track rows (light, name, arm, mute, solo,
input, channel, group, output placeholder) beside lanes under a bar ruler,
MIDI in through uw-core (CoreMIDI checked; ALSA built and self-tested), routing to
tracks, the MIDI monitor, and the grid in its own test window. No uw-core
changes were needed. Playing real MIDI into it is the author's.

The main window becomes a list of track rows, as StageHand's rack rows
(its `docs/rack-view.md`): a view model and a controller with no toolkit
types, intents from the widgets, the session as the source of truth.
- A track model: name, input (All, a device; channels and UMP groups, as
  StageHand's row input side), record arm, mute and solo, and an output
  that's only a placeholder until milestones 7 and 8.
- Each row: its name, the input choices, an arm button, mute and solo, the
  output, and a MIDI activity light. Add, remove and rename tracks.
- Beside the rows, each track's lane on a timeline (bars and beats), empty
  until milestone 6.
- MIDI input through `uw::midi`: ALSA sequencer UMP; MIDI 1.0 hardware is
  converted to UMP on the way in. Input reaches the tracks whose input
  matches it.
- A small MIDI monitor panel (ImGui), to see what arrives from KeyEDO.
- The milestone 4 grid stays reachable (View menu) until milestone 6 gives
  it clips to open.

## 6. Transport, the clip model and recording (Umpire): done on Linux 2026-10-23

[recording.md](recording.md). The transport in the menu bar, clips of UMP in
ticks (40320 a quarter), takes stored as MIDI 2.0, undo for every command,
input moved to the session with a status line, sparse rows, takes growing
on the armed lanes, clips selected, moved, deleted and opened in the grid,
whose rows are now EDO steps (StageHand's tuning). Built and self-tested on
Linux only: the Mac build and recording real MIDI by hand are the author's
(recording.md, "Still to do"). Not split: undo and the rows fit in the day.

Revised 2026-10-23 for the UI direction (decisions.md, "The UI direction";
[ui-direction.md](ui-direction.md)). If it proves more than a day, split off
undo and the rows.

- Umpire's transport: play, stop, record, locate, and a tempo, with a
  position in ticks. A small toolbar shows it.
- A clip model: UMP messages with timestamps in ticks, with room for
  per-note data (decisions.md, "Deferred"). No toolkit types. Clips sit on
  lanes through clip references, with room for transforms (idea 9) and for
  an instrument of the clip's own, empty until milestones 7 and 8.
- Undo: every change is a command on the session (add, remove and rename
  tracks, arm, mute and solo, record, move and delete clips).
- Input moves to the session: Umpire listens to all inputs, playing goes to
  the touched track and recording to the armed tracks. Tracks get stable
  IDs. A status line names what's listening. Milestone 5's per-track input,
  channel and group choices go.
- Sparse rows: a light, the name, and badges only for what isn't the
  default. The touched track shows the rest in place. An empty session shows
  only the prompt.
- Recording: while recording, each armed track records what you play into a
  new clip on its lane, drawn as it grows.
- Clips on the lanes: select, move along the lane, delete. Double-clicking a
  clip opens it in the grid (milestone 4's), in its own window as
  StageHand's sequence editor, showing its notes.

## 7. Playback to MIDI outputs (uw-core, then Umpire)

- ALSA sequencer UMP **output** in `uw::midi` (StageHand only has input).
- A track's output can be a MIDI device. A scheduler, on its own thread,
  sends the tracks' clips to their outputs on time; loop and locate.
- Play to hardware and to KeyEDO's synths. The author tests anything
  audible.

## 8. Hosting CLAP instruments (Umpire)

- JACK audio through `uw::audio`, and a track's output can be a CLAP
  instrument through `uw::clap-host`, chosen from the scanned plugins in its
  row. The transport and the live input feed the plugin's events, with
  sample-accurate timing, as StageHand's engine does.
- Translate MIDI 2.0 to MIDI 1.0 only for plugins that need it (`uw::ump`).
- Design the engine so it can also run offline, driven by a render loop
  instead of the audio device. Milestone 14 builds on that, for printing and
  the mixdown (decisions.md, "Audio").

## 9. Files: MIDI Clip Files and the session (Umpire)

- Read and write standard MIDI Clip Files (M2-116-U), with tests that
  round-trip clips, including Pitch 7.9 notes.
- Umpire's session file (JSON, as StageHand's files are): tracks, their
  instruments, and clip references with their own instruments. Leave room for audio file
  references (decisions.md, "Audio"; milestones 15-17).
- Open and Save from the menu, through DPF's file browser or StageHand's
  zenity/kdialog approach.

## 10. The clip editor: the piano roll (Umpire)

Bring StageHand's sequence editor drawing to the grid that a clip opens in,
editing the clip model: select, move, resize, delete, velocity, undo. Show
Pitch 7.9 notes on an EDO grid, as StageHand does. Include a plan for showing
per-note pitch bend and controllers later (decisions.md, "Deferred"). Also
zooming into a clip's lane to edit its notes in place (ui-direction.md),
sharing the grid's editing code.

## 11. Plugin editor windows (uw-core, then Umpire)

Move StageHand's plugin editor window (X11 embedding; `EditorWindowMac` for
later) into uw-core, and open editors from a track's row. Plugin state is
saved in the session file.

## 12. UMP groups and the arrangement (Umpire)

- A track is a view over a range of UMP groups (decisions.md, idea 5), with
  routing to outputs by group. Input filtering by group or channel is asked
  for through the palette, not shown on every row (ui-direction.md).
- The arrangement: the lanes' clips become clip references, with transforms
  on the reference (idea 9): offset, transpose, loop.

## 13. First useful version (Umpire)

The decisions.md scope from end to end with KeyEDO: record, edit, play to a
plugin and to hardware, save, reopen. Fix what that turns up. Next comes the
audio phase (below). Where the macOS port fits is decided then.

## After the first useful version: audio

Decided 2026-10-14 (decisions.md, "Audio: printing, mixdown, playback").
Plugin tracks come first; hardware comes last.

### 14. The mixer and offline rendering (Umpire)

- Per-track gain, pan, mute and solo, and a master bus, in the engine and on
  screen (ImGui).
- The engine renders offline: the transport and plugins are driven by a
  render loop, faster than real time, without the audio device. Tests
  compare offline and real-time renders of the same session, in memory and
  silent.

### 15. Printing a plugin track (Umpire)

- Render a CLAP instrument track to an audio file (WAV, 32-bit float, the
  session's rate) in the session's folder, linked to its track.
- Only on the user's command (decisions.md, "Audio"): no automatic or
  background printing.
- Record what made it (a hash of the clips and the plugin's state), so the
  view can show when the print is out of date. An out-of-date print stays as
  it is until the user prints again.
- Choose the WAV library (decisions.md, "Audio", open questions).

### 16. The track's audio view (Umpire)

- A printed track has two views, MIDI and audio (its waveform, from a peak
  cache so long files draw fast), stacked and in time with each other.
  Either can be on top; the one underneath shows as a slight overlap.
- Swapping them: a click on the edge that shows, and a key.
- Decide whether what plays (audio or instrument) follows the view on top.

### 17. Audio playback and the mixdown (Umpire)

- Play printed audio in the transport, in place of the instrument when
  chosen (freeing the plugin).
- Mix the session to a file through the mixer (offline), with a choice of
  formats, and play the mix in the app.

### 18. Printing hardware tracks (uw-core, then Umpire)

- Audio inputs in `uw::audio`: JACK capture ports, CoreAudio input.
- Record a hardware or KeyEDO track's audio in real time while its MIDI
  plays, with latency compensation (measured round trip), into the same kind
  of print as milestone 15.
- Audible: the author tests it.

## Outside Umpire's roadmap

These are separate work in other repos, and can be done whenever:

- **StageHand switches to uw-core** (StageHand): done 2026-10-07 on
  StageHand's branch `uw-core-switch`, from `macos-milestone-6`. It's on
  uw-core `a200ba4`. StageHand's CI passes on Linux and macOS, building
  uw-core's CoreAudio, CoreMIDI and CLAP code on macOS without warnings
  (uw-core's `docs/macos-check.md`, Results). On 2026-10-08, uw-core's own
  build and tests passed on the Mac (checklist steps 1 and 2: no warnings, 5
  suites, and the UMP-only build). So did step 3, StageHand over uw-core on
  the Mac: no warnings, 11 suites, the listings, the virtual inputs, and the
  author's audible checks (`--tone`, and a rack with a keyboard and an
  editor). Only the click on a second device (the aggregate device) is
  untested, because the Mac has no second output. Merged into StageHand's
  `main` on 2026-10-08 (`afb60f2`), still pinned to uw-core `a200ba4`. CI checks out the private uw-core with a
  `SUBMODULES_TOKEN` secret: a read-only token for uw-core and StageHand,
  expiring 2026-12-31.
- **StageHand switches to the DPF fork** (StageHand): done 2026-10-22 in
  StageHand `b65ba55`. `external/dpf` is now a submodule, the fork at
  `88683252`, which pins the pugl fork. The vendored copy and its
  `README.StageHand.md` are gone (the fork's `README.fork.md` has its
  patches). StageHand leaves `DGL_SWAP_INTERVAL` unset. CI's
  `SUBMODULES_TOKEN` now needs read access to DPF and pugl too.
- **Plugins move onto the DPF fork, then use `uw::ump`** (the plugin
  roadmap, `plugins-roadmap.md`): diff each plugin's `dpf` against upstream,
  merge the changes into the fork, and give each plugin its own
  `DGL_NAMESPACE`. The namespace is needed now: on the Mac on 2026-10-08, the
  9 installed DPF plugins all defined pugl's Objective-C classes as
  `DGL_17000604_Pugl*`, and the runtime warned about the clash when one host
  (uw-core's `test-clap-host`) loaded them together.
