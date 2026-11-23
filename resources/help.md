<div style="display: flex; align-items: center; gap: 0.75rem; margin: 1.5rem 0 0.5rem;">
  <a href="https://www.unlikelyware.com" target="_blank" rel="noopener noreferrer">
    <img src="Unlikelyware.png" alt="Unlikelyware" style="width: 56px; height: auto; border-radius: 10px; margin: 0; display: block;">
  </a>
  <h1 style="margin: 0;">Umpire</h1>
</div>

<p>
  <em>Umpire is an instrument plugin for macOS and Linux that records, edits and plays MIDI 2.0 inside your DAW, with exact pitch in any EDO and per-note expression kept.</em>
</p>

::: {.callout .callout-note .card}
## On This Page
[Overview](#overview) · [Getting Started](#getting-started) · [The Window](#window)

**Working —** [Recording](#recording) · [The Host's Own Clip](#host-clip) · [The Lane of Takes](#lane) · [Editing Notes](#editing) · [Tuning](#tuning) · [MIDI Effects](#midi-effects) · [Takes as Files](#files)

**Hosts —** [Hosts](#hosts) · [The Setup File](#setup) · [Troubleshooting](#troubleshooting)
:::

::: {.callout .callout-note .card}
## Overview {#overview}
DAWs hand plugins MIDI 1.0, or notes of their own, and round pitch to semitones. Umpire goes around them: **the notes never pass through the host.**

- **Umpire opens the MIDI 2.0 input itself:** a MIDI 2.0 device, or KeyboardEDO.
- **It hosts the synth itself:** any CLAP instrument, sent MIDI 2.0 where it takes it.
- **It gives the host audio.** To the host it's an ordinary instrument on a track.

The host still does what it's good at: its transport and tempo drive recording and playback, its project saves Umpire's takes and the synth's patch, and its mixer, effects and rendering take Umpire's sound like any other track's.
:::

::: {.callout .callout-note .card}
## Getting Started {#getting-started}
1. **Put Umpire on an instrument track** in your host (REAPER, Bitwig, Ardour, Logic or MainStage).
2. **Open its window and choose the synth** with the **▾** beside the synth's name in the header. The first time, Umpire looks through every CLAP plugin installed, which takes a moment.
3. **Choose the input** with the **▾** beside it, at the header's right. Left alone, Umpire takes KeyboardEDO's if it's there, else the first MIDI 2.0 source, else the first hardware one.
4. **Play.** You hear the synth through the track, whether the host plays or not.
5. **Turn Record on** (the red button), then record in the host: the take appears on Umpire's lane.

The host may record a clip of its own on Umpire's track as well. That does no harm, though it can be confusing: see [The Host's Own Clip](#host-clip).
:::

::: {.callout .callout-note .card}
## The Window {#window}
**The header:** the Unlikelyware logo (a click opens unlikelyware.com), **Umpire**, and **Help** at the right (the **?** box that opens this page).

**The bar below it:**

- **Record:** red when on. Umpire records only while it's on and the host records (or, in some hosts, plays: see [Recording](#recording)).
- **Edit:** undo and redo, and the commands for notes and takes.
- **MIDI FX:** the MIDI effects in front of the synth ([MIDI Effects](#midi-effects)).
- **The synth's name:** a click opens the synth's own editor in a window of its own; **▾** chooses another synth.
- **At the right:** the input and its **▾**, **Recording** while a take records, and the host's position (bar.beat.sixteenth).

**Below the bar:** the [lane of takes](#lane) in the host's bars, then the chosen take's [grid](#editing), with the inspector (Clip, Tuning, Note) at its right.

**Resizing:** drag the host window's corner.
:::

::: {.callout .callout-note .card}
## Recording {#recording}
**Umpire records while the host records and Record is on.** Record is off when Umpire is added; turn it on with its button, or as the **Record** parameter in the host (it can be automated).

- **Every take is kept.** Where takes overlap, the newest is heard.
- **Loop recording works:** a jump while recording (the host's loop going round) starts a new take.
- **Playback follows the host:** start anywhere and notes already sounding there are played from their start, Pitch 7.25 included.
- **To record other tracks while Umpire plays** (printing its sound to audio, say), leave its Record off: it plays its takes as usual.
- **Logic and GarageBand** don't tell a plugin when they record, so there a take starts when the host plays with Record on; a pass where you play nothing leaves no take.
- **MainStage and StageHand** are for playing live: nothing records there, and the button stays off.
:::

::: {.callout .callout-note .card}
## The Host's Own Clip {#host-clip}
Umpire's takes live inside Umpire, not on the host's timeline. But a host records into an armed track as usual, so it may make a clip of its own there as well: empty, or holding whatever reached the track's own MIDI input.

- **It does no harm.** Umpire doesn't listen to the host's notes, so that clip plays nothing, and editing it changes nothing. It's only confusing to have two recordings of one performance, one of which does nothing.
- **Some hosts won't record at all without an armed track** (Ardour), so there you'll get one anyway. Delete it, or ignore it.
- **Where a host lets you, keep it from making one:** in Bitwig, turn off the track's record arm (Umpire's recording follows the transport, not the arm); in REAPER, leave the track armed with monitoring on and set its record mode to **Record: disable (input monitoring only)**.

**Why it's like this:** today's DAWs hand plugins MIDI 1.0, or note events of their own, and round pitch to semitones on the way. Umpire takes its notes straight from the MIDI 2.0 input instead of from the track, which is what keeps exact pitch and per-note expression. It's a workaround, and a little awkward, but until DAWs pass MIDI 2.0 through to plugins it's the way to have MIDI 2.0 in them.
:::

::: {.callout .callout-note .card}
## The Lane of Takes {#lane}
The lane shows the takes in the host's bars: a ruler, the takes heard (each with a sketch of its notes), and under them a strip for the parts of older takes that newer ones cover.

- **Click a take** to show it in the grid; click empty lane for none.
- **Click the strip** to choose the newest covered take there; click again for the next older.
- **Right-click a take**, or use the **Edit** menu, for **Bring take to front**, **Delete take**, **Rename take...**, **Export take...** and **Import clip...**
- **Scroll** to move through time; **Command-scroll** (Ctrl-scroll on Linux) or a pinch zooms the lane. While the host plays, the lane keeps the playhead in view until you scroll by hand.
:::

::: {.callout .callout-note .card}
## Editing Notes {#editing}
The grid has a row for each step of the take's EDO, and a column for each sixteenth. Edits are Umpire's own, with their own **Undo** (Edit menu, or Cmd+Z / Ctrl+Z when the window has the keys): the host's undo doesn't undo them.

| Gesture | Result |
|---|---|
| Click empty grid | Adds a note |
| Click a note | Selects it |
| Shift-click | Adds a note to the selection, or takes it out |
| Drag on empty grid | Selects the notes in the box (Shift: adds them) |
| Drag notes | Moves them by whole sixteenths, each keeping its offset from the grid |
| Shift-drag | Slides them freely in time |
| Cmd-drag (Ctrl on Linux) | Puts the first note's start on the nearest sixteenth |
| Drag a note's right end | Changes the length (Shift: freely) |
| Right-click a note | Deletes it, or the selection it's in |
| Up, Down | Moves the selection a row (Shift: an octave) |
| Left, Right | Moves it a sixteenth |
| Delete, Backspace | Deletes it |
| Q | Quantizes the selected notes' starts |
| Scroll, Shift-scroll | Scrolls the rows, or through time |
| Command-scroll (Ctrl on Linux), pinch | Zooms the columns |

**A recorded note moved in time keeps its feel:** moving isn't quantizing. Q, and Cmd-drag, quantize on purpose.

**The Note panel** sets the selected notes' velocity, and moves or resizes the focus note (the one with the thicker edge) and the rest with it.

**Hearing notes:** a note sounds through the synth while you hold the mouse on it, whether the host plays or not. **Edit > Hear notes while editing** turns it off.
:::

<details class="disclosure">
<summary class="disclosure-title subtle">Keys in the host</summary>

::: disclosure-content
Hosts may keep keys for themselves. A click in the grid gives Umpire the keyboard where the host allows it.

- **REAPER:** turn on **Send all keyboard input to plugin** (in the FX window's menu) for the grid's keys, or use the Edit menu and right-click.
- **Renaming** a take or clip uses a text box of the system's own, since some hosts keep the keys a text field needs.
:::
</details>

::: {.callout .callout-note .card}
## Tuning {#tuning}
**A take's EDO is worked out from its notes:** the Tuning panel says **Worked out from the notes**, **Not sure** (with the EDOs that fit), or **No EDO fits** (the notes go on the nearest rows).

- **EDO** (5 to 72) and **Step 0 at** (the MIDI note of step 0) set it by hand: the panel then says **Set by hand**. Each change is one undo step.
- **The notes keep their exact pitch.** The grid only shows them on its rows; a note between rows says how many cents it is from its row in the Note panel.
:::

::: {.callout .callout-note .card}
## MIDI Effects {#midi-effects}
The host's own note effects never reach Umpire, since its notes don't come from the host. Instead, **CLAP note effects go inside Umpire**, in front of the synth: Retune, NoteGran, EDOSpread12 or others'.

- **MIDI FX** in the header lists them, in order. **Add a MIDI effect...** adds one after the others; each has **Editor**, **Bypass**, **Move up**, **Move down** and **Remove**.
- **A take records what you play**, before the effects, so they act on it again at playback: changing or bypassing an effect changes what a take sounds like. The grid shows what was played.
- **Notes you hear while editing** go through the effects too.
- **MIDI FX** is dimmed when there are none or all are bypassed, and in the accent colour when one isn't installed here.
:::

::: {.callout .callout-note .card}
## Takes as Files {#files}
- **Export take...** (Edit menu or a take's right-click) saves a take as a MIDI Clip File (`.midi2`), the MIDI 2.0 file format, with its per-note data.
- **Import clip...** puts a MIDI Clip File on the lane at the playhead, as a new take.
:::

::: {.callout .callout-note .card}
## Hosts {#hosts}
| Host | Format | Notes |
|---|---|---|
| REAPER | CLAP | To keep REAPER from recording a clip of its own ([why](#host-clip)), leave Umpire's track armed with monitoring on and set its record mode to **Record: disable (input monitoring only)**. Turn on **Send all keyboard input to plugin** for the grid's keys |
| Bitwig | CLAP | Turn off the track's record arm, so Bitwig doesn't record a clip of its own ([why](#host-clip)); Umpire still records |
| Ardour | VST3 | Ardour records only with an armed track, so it records a region of its own too ([why](#host-clip)): ignore or delete it. Open Umpire's window once each session if the synth needs timers |
| Logic, MainStage | AUv2 | A take starts when Logic plays with Record on |

**On Linux with PipeWire,** the block size comes from PipeWire, not the host: start the host with `pw-jack -p 256` (or set PipeWire's quantum) for a short delay between playing and hearing.

**Saving:** the host's project keeps Umpire's takes, the synth chosen and its patch, the input, the MIDI effects, and the lane's and grid's zoom. A project's synth that isn't installed shows as **not installed**: nothing sounds, and its patch is kept for when it is.
:::

::: {.callout .callout-note .card}
## The Setup File {#setup}
A new Umpire starts with the synth, input and recording the setup file says; each one's own choice is saved with the project after that. It lives at `~/Library/Application Support/Umpire/plugin.conf` on the Mac and `~/.config/umpire/plugin.conf` on Linux, one setting a line:

- `input = KeyboardEDO`: part of a source's name, or `none`.
- `synth = PlainOldSynth`: a `.clap`'s name in CLAP's folders, or a path, or `none`.
- `record = host`, `play` or `off`:
  - **record = host:** a take records while the host records (the default in most hosts).
  - **record = play:** while the host plays, with Record on (the default in Logic and GarageBand).
  - **record = off:** nothing records: Umpire is a player (the default in MainStage and StageHand).
:::

::: {.callout .callout-note .card}
## Troubleshooting {#troubleshooting}
- **No sound:** choose a synth with its **▾**; check the input's name (**(gone)** means its device isn't there; Umpire opens it again when it comes back).
- **Nothing records:** turn Record on, and record in the host; in Logic, play with Record on.
- **Notes late in Ardour, or a synth stalls:** open Umpire's window once in the session.
- **Long delay on Linux:** see PipeWire under [Hosts](#hosts).
- **The log** says what Umpire did, one line per event: `~/Library/Logs/Umpire/plugin.log` on the Mac, `~/.local/state/umpire/plugin.log` on Linux. Attach it to a bug report at [GitHub Issues](https://github.com/erickampman/UmpireDAW/issues).
:::
