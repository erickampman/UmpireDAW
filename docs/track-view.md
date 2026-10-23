# The track view and MIDI in (milestone 5)

The main window is now the track view (decisions.md, "The track view"): one
row per track, where its MIDI comes from and where it goes, beside its lane
on a timeline, with a MIDI monitor along the bottom. MIDI arrives through
uw-core's input seam and reaches the tracks whose input takes it. Built and
checked on the Mac on 2026-10-23 (CoreMIDI). **Not yet built on Linux** (the
ALSA sequencer path), like milestone 4.

The UI direction decided afterwards ([ui-direction.md](ui-direction.md))
changes the rows: milestone 6 moves input to the session, drops the per-track
input, channel and group choices, and shows only what isn't the default.

## Building and running

[building.md](building.md) has both platforms. The build now needs uw-core's
backends (`UW_CORE_BACKENDS` ON): on Linux that's JACK and alsa-lib 1.2.10 or
later, as StageHand.

```bash
ctest --test-dir build                     # silent: the grid model, the track controller
build/umpire-daw                           # the track view
build/umpire-daw --self-test /tmp/shots    # both windows, scripted (silent; don't play MIDI meanwhile)
```

## What's in it

| Piece | Where | Toolkit |
|---|---|---|
| `Track`, `MidiSourceRef` | `src/App/Track.h` | no |
| `MidiInputs`: the platform input, opened, queued, the input choices | `src/App/MidiInputs.*` | no |
| `TrackController`, `TrackViewModel` | `src/App/TrackController.*`, `TrackViewModel.h` | no |
| `describeUmp` (the monitor's text) | `src/App/MidiDescribe.*` | no |
| `TrackCanvas`: the ruler and lanes (NanoVG) | `src/Ui/TrackCanvas.*` | DGL |
| `TrackPanels`: menus, track headers, monitor (ImGui) | `src/Ui/TrackPanels.*` | ImGui |
| The test grid window (milestone 4's grid) | `src/Ui/Grid*`, `MainWindow.cpp` | DGL, ImGui |

- **StageHand's pattern, not its code.** Its rack view (RackViewModel,
  RackController, RackWidget: about 7,000 lines) is bound to its engine,
  session and NanoVG rows. Umpire follows the same shape: a view model of
  plain values rebuilt each tick and compared, so the window redraws only on
  a change; widgets send intents to a toolkit-free controller; tests drive
  the controller headless with a fake `uw::MidiInput`. Copied as they are:
  `MidiSourceRef` (without its sequence kind), the filter's group and
  channel masks, the rule for which inputs to open, and the monitor's
  `describeUmp`. That settles the open question in decisions.md.
- **Inputs, as StageHand opens them:** two virtual inputs ("MIDI 2.0 In",
  "MIDI 1.0 In", shown as "Umpire MIDI 2.0 In") for apps such as KeyEDO, and
  every hardware input. A software source (KeyboardEDO's own) is opened when
  a track chooses it. Hotplug: newly connected hardware is opened, and a
  track whose device left shows "(not connected)" in red. MIDI 1.0 sources
  arrive as UMP MT 0x2 (uw-core wraps them losslessly); MIDI 2.0 as MT 0x4.
  Nothing is translated yet: the clip model (milestone 6) decides what's
  stored.
- **A track takes a message** when its input matches the source (All, a
  device, a virtual input, or None), the message's group is in its group
  mask, and, for channel voice messages, its channel is in its channel mask.
  Utility and stream messages go to no track. Several tracks can take the
  same message.
- **A track's row** (ImGui, the house look): an activity light (teal, lit
  for 0.15 s after a message), the name (click to type, Enter to keep), R
  (arm, red), M (mute, amber), S (solo, teal); then the input, channel and
  group choices and the output ("No output" until milestones 7 and 8). The
  selected track has the accent down its edge and a lighter lane; an armed
  track's lane has a faint red wash.
- **Its lane** (NanoVG): bar and beat lines under a bar ruler, 4/4 until the
  transport has a meter (milestone 6). The wheel scrolls the rows (over the
  headers too: ImGui passes it to the canvas); Shift or a sideways wheel
  scrolls time.
- **Keys** (the canvas's): Up and Down select, R arms the selected track,
  Cmd/Ctrl+T adds a track, Cmd/Ctrl+Q quits. Track > Add track adds after
  the selected one; Track > Remove track removes it.
- **The MIDI monitor** (View > MIDI monitor, on by default): the latest 200
  messages, with time, source, the packet in hex, StageHand's description,
  and the tracks that took it.
- **The test grid window** (View > Test grid window): milestone 4's grid in
  its own window, as a clip's grid will open (decisions.md). File > Close
  window closes it. It goes when milestone 6 opens clips.
- **Escape now closes ImGui menus and dropdowns.** ImGui does that through
  keyboard navigation, which Umpire's widget turns off (ui-spike.md,
  finding 4), so the widget closes them itself.
- **House widgets added:** `toggle()` (arm, mute, solo, the +) and
  `light()`, in `Widgets.{h,cpp}` after KeyboardEDO's, and a red
  (`colour::kRecord`) for arming.

## Checked

On the Mac (macOS 26, scale factor 2, CoreMIDI), 2026-10-23:
- no warnings; `test-track-controller` (20 checks: the inputs opened, the
  choices, routing by device, virtual input, None, channel and group, the
  lights, the monitor, arm, mute, solo, names, removing, no MIDI at all) and
  `test-grid-model` pass;
- `--self-test`: the track view's checks (adding tracks three ways, menus
  and Escape, selecting by lane, header and keys, arm, mute, renaming, the
  input choice, an injected message lighting the tracks and reaching the
  monitor, the wheel over lanes and headers, alignment while scrolled,
  hiding the monitor, a resize, no frames while idle) and then the test
  grid window's, as milestone 4's. All pass. The input choice listed the
  Mac's real sources (a network session, KeyboardEDO).
- Under the main loop: a few frames at startup, none while idle; SIGINT
  quits.

Not checked: **real MIDI arriving** (nothing was played: that's the
author's, with KeyEDO or a keyboard), and Linux.

## Still to do, by the author

- Build on Linux; `--self-test` there.
- Play into it: a hardware keyboard (MIDI 1.0), KeyEDO into "MIDI 2.0 In",
  and see the lights and the monitor; a track on one device, another on a
  channel; unplug and replug a device.
- Whether the row's layout and the lane look right (and the colours,
  skeleton.md).
