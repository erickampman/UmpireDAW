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

## 3. The DPF fork (DPF fork)

Make the private DPF fork in `~/dev/dev-common`: upstream DPF plus
StageHand's vendored patches (PMU's and StageHand's). Add the vsync patch
(ui-spike.md, finding 6): set `PUGL_SWAP_INTERVAL` to 0 when DGL makes its
view, behind a define so each user can choose. Write down each local patch
in the fork's README, as StageHand's `README.StageHand.md` does. Build a DGL
example on Linux. The plugins' own changes come later, in the plugin roadmap.

## 4. Umpire's skeleton (Umpire)

Umpire's own CMake project builds against uw-core and the DPF fork as
submodules, replacing `UMPIRE_DPF_DIR`. It has:
- the `umpire-daw` binary with a main loop driven like StageHand's;
- one main window: the NanoVG canvas plus **Umpire's own ImGui widget**,
  adapted from DPF-Widgets' (ui-spike.md, proposal 2), with the spike's
  routing, on-demand redraw and rescale;
- the view-model rule: no DGL or ImGui types below the drawing layer.

The spike (`spike/`) is retired here or kept as a test bench. Decide at the
start of the milestone.

## 5. MIDI in and the clip model (Umpire, maybe uw-core)

- A clip model: UMP messages with timestamps in ticks, with room for
  per-note data (decisions.md, "Deferred"). It has no toolkit types.
- MIDI input through `uw::midi`: ALSA sequencer UMP. MIDI 1.0 hardware is
  converted to UMP on the way in.
- Record into a clip in memory, and show it live in the grid.
- A small MIDI monitor panel (ImGui), to see what arrives from KeyEDO.

## 6. Files: MIDI Clip Files and the session (Umpire)

- Read and write standard MIDI Clip Files (M2-116-U), with tests that
  round-trip clips, including Pitch 7.9 notes.
- Umpire's session file (JSON, as StageHand's files are): tracks and clip
  references.
- Open and Save from the menu, through DPF's file browser or StageHand's
  zenity/kdialog approach.

## 7. The piano roll (Umpire)

Bring StageHand's sequence editor drawing to Umpire's canvas, editing the
clip model: select, move, resize, delete, velocity, undo. Show Pitch 7.9
notes on an EDO grid, as StageHand does. Include a plan for showing per-note
pitch bend and controllers later (decisions.md, "Deferred").

## 8. Transport and playback to MIDI outputs (uw-core, then Umpire)

- ALSA sequencer UMP **output** in `uw::midi` (StageHand only has input).
- Umpire's transport: play, stop, locate, loop, and a tempo. A scheduler
  that sends a clip's UMP to an output on time, on its own thread.
- Play to hardware and to KeyEDO's synths. The author tests anything
  audible.

## 9. Hosting CLAP instruments (Umpire)

- JACK audio through `uw::audio`, and one CLAP instrument per track through
  `uw::clap-host`. The transport feeds the plugin's events, with
  sample-accurate timing, as StageHand's engine does.
- Translate MIDI 2.0 to MIDI 1.0 only for plugins that need it (`uw::ump`).

## 10. Plugin editor windows (uw-core, then Umpire)

Move StageHand's plugin editor window (X11 embedding; `EditorWindowMac` for
later) into uw-core, and open editors from Umpire's track list. Plugin
state is saved in the session file.

## 11. Tracks, UMP groups and the arrangement (Umpire)

- A track is a view over a range of UMP groups (decisions.md, idea 5), with
  routing from inputs and to outputs by group.
- The arrangement: a linear timeline of clip references, with transforms on
  the reference (idea 9): offset, transpose, loop.
- An arrangement view on the canvas, above or beside the piano roll.

## 12. First useful version (Umpire)

The decisions.md scope from end to end with KeyEDO: record, edit, play to a
plugin and to hardware, save, reopen. Fix what that turns up. Then decide
what comes next: the macOS port (following StageHand's), or the deferred
features.

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
- **StageHand switches to the DPF fork** (StageHand), after milestone 3.
- **Plugins move onto the DPF fork, then use `uw::ump`** (the plugin
  roadmap, `plugins-roadmap.md`): diff each plugin's `dpf` against upstream,
  merge the changes into the fork, and give each plugin its own
  `DGL_NAMESPACE`. The namespace is needed now: on the Mac on 2026-10-08, the
  9 installed DPF plugins all defined pugl's Objective-C classes as
  `DGL_17000604_Pugl*`, and the runtime warned about the clash when one host
  (uw-core's `test-clap-host`) loaded them together.
