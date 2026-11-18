# Umpire as a plugin

**A proposal, 2026-11-18, on the Mac; not yet decided.** What's left of the
standalone plan (milestones 12 to 18, the tempo map and stem export, plugin
routing) is most of a DAW, and the author has to build it alone. This
proposes a smaller route to the same goal: put Umpire inside a host that
already does the DAW's work, and keep only what no host does, which is
recording, editing and playing MIDI 2.0 with its per-note data and EDO
pitch.

The obvious objection is that the hosts don't do MIDI 2.0. That's true, and
this proposal doesn't depend on them doing it (below).

## The hosts don't do MIDI 2.0

Retune's dialect logs (ReleaseUMP R2-7; Retune `docs/design.md`):

- **REAPER 7.79** (2026-11-09) offers plugins the `CLAP | MIDI` note
  dialects, not MIDI2, and sends MIDI 1.0 Note On/Off. Per-note pitch is
  rounded to semitones.
- **Bitwig Studio 6.1.3** (2026-10-21) offers `CLAP | MIDI | MIDI_MPE`, not
  MIDI2, and sent Retune MIDI 1.0. Its CLAP dialect keeps a tuning
  expression, but that's CLAP, not UMP: no Pitch 7.25, no per-note
  controllers as MIDI 2.0 has them.

So a plugin that takes its notes from the host gets MIDI 1.0. Cubase does
more MIDI 2.0, but it costs money and doesn't run on Linux.

## The idea: the notes never pass through the host

The plugin is an instrument, as far as the host can tell. Inside it:

- **It opens the MIDI 2.0 input itself**: the device, or KeyboardEDO's
  port, through uw-core's `AlsaSeqInput` or `CoreMidiInput`, as Umpire
  does now. The host's own MIDI input is not used.
- **It hosts the synth itself**, through uw-core's CLAP host
  (`uw::ClapPlugin`), and sends it MIDI 2.0 where the synth's note port
  takes it, as Umpire's `AudioEngine` does now (plugins.md). It can pass on
  the host's transport too, which Umpire's own engine doesn't yet.
- **It records and plays back** against the host's transport: it records
  while the host records, and plays back from the host's position.
- **Its output is audio.** The host gets a stereo instrument and nothing
  else.

```
MIDI 2.0 device ──► [ the plugin: record · edit · play ──► hosted synth ] ──► audio ──► host
                                         ▲
                            host transport, tempo, state
```

How the host treats MIDI no longer matters, because it never sees the notes.

## What the host does

- **Transport, tempo and meter.** Umpire has one tempo and 4/4; the host
  has a tempo map. Clips are in ticks (beats), so they follow the host's
  tempo changes without any work. T1 isn't needed.
- **Mixing and audio effects** after the plugin: gain, pan, sends, buses.
  Milestone 14 isn't needed.
- **Saving.** The plugin's clips and the hosted synth's state go in the
  plugin's state, which the host saves in its project.
- **Stems.** The host renders, freezes or bounces each track. Milestones 15
  to 18 and stem export (S1, S2) aren't needed, nor the tempo-map file,
  since the stems never leave the host that has the tempo map.
- **Arranging the audio**, mastering, and everything else stem export was
  for.

**Hosts:** REAPER and Bitwig, both on Linux and the Mac, both owned
already, both CLAP. Neither is open source; see the next section.

## Open-source hosts

REAPER and Bitwig aren't open source, and one reason for Umpire is that
recording MIDI 2.0 shouldn't need a closed DAW. The plugin keeps that: the
part only Umpire does (recording, MIDI 2.0, the clip files, hosting the
synth) is the plugin, under MIT. The host only supplies a transport,
mixing, saving and rendering, through open plugin standards, so it can be
swapped. Open-source hosts that might do, none checked yet:

- **Ardour** (GPL; Linux and the Mac), through clap-wrapper's VST3 build.
  In the fork (`~/Dev-Common/clap-wrapper`), the VST3 side turns
  `ProcessContext::kRecording` into `CLAP_TRANSPORT_IS_RECORDING`
  (`src/detail/vst3/process.cpp`), so the host's record button reaches the
  plugin, and the plugin gets CLAP `timer-support` and `posix-fd-support`
  (`src/clap_proxy.cpp`), through VST3's `IRunLoop` on Linux. The risk:
  `IRunLoop` comes with the plugin's view, so the hosted synth's timers and
  file descriptors might only be served while the plugin's editor is open.
  The spike checks it.
- **Qtractor** (Linux), which hosts CLAP, as far as known; to check.
- **Carla** (Linux), a plugin host with CLAP support, experimental as far
  as known; to check.
- **Zrythm**: CLAP and VST3 support not known; to check.
- **StageHand**, the author's own CLAP host: no recording with a transport
  and no render today, but the one host fully under the author's control.

Ardour is in the spike's checks beside REAPER and Bitwig, not left to the
host checks at the end: if it works, the open-source question is mostly
answered. If no open-source host works, the standalone app is still there,
sharing `src/App`.

## What stays Umpire's

What the host can't do, which was always the point of Umpire:

- Recording MIDI 2.0 as UMP, with per-note data and per-key Pitch 7.25
  (recording.md).
- The grid on the EDO, editing notes with their per-note data
  (clip-editor.md).
- Playing to a CLAP synth, chasing Pitch 7.25 on play, locate and loop
  (playback.md).
- MIDI Clip Files: the clips are stored as M2-116-U in the plugin's state,
  and a clip can still be exported as a file.

## What carries over

| Code | Today | In the plugin |
|---|---|---|
| `Clip`, `ClipEdit`, `ClipFile`, `GridModel`, `Scale`, `History` | `src/App` | As they are: they need only `uw::ump` |
| `Player` | Umpire's `Transport` and clock; MIDI outputs and the engine | The host's position each block; plays only to the hosted synth |
| `AudioEngine` | A voice per track, run by the audio backend | The plugin's `process()`: one voice, run in the same block, events at their sample offsets as `renderBlock()` places them now |
| MIDI input | `MidiInputs` over uw-core | The same uw-core inputs, opened by the plugin; events to the audio thread through `SpscQueue` |
| Hosted synth | `uw::ClapPlugin`; `HostServices` from `MainLoop` | The same; `HostServices` from the host's own CLAP `timer-support` and `posix-fd-support` |
| The synth's editor | `uw::EditorWindow` | The same, as its own window |
| `GridCanvas`, `GridPanels` | A grid window per clip | In the plugin's editor (DGL, as Retune's editor uses DPF) |
| Not needed | | `MainLoop`, `MainWindow`, the track view (one plugin per track now), `Transport`, `SessionFile`, the audio backends |

`MidiOutputs` (hardware out) could stay: the plugin could play to a MIDI 2.0
device as well as to its synth. That's later, if at all.

## How it's built

**As Retune is: its own CLAP entry point, DPF only for the editor,
clap-wrapper for VST3 (and AUv2 on the Mac).** An entry point of its own is
needed, not DPF's plugin wrapper:

- **Recording** follows the host's record button through
  `CLAP_TRANSPORT_IS_RECORDING`. DPF's `TimePosition` has `playing` but no
  recording flag.
- **The hosted synth's main-thread needs** (timers, file descriptors, editor
  windows on Linux) are met by passing on the host's `timer-support` and
  `posix-fd-support`, which DPF doesn't give its plugins.
- **Offline render**: the CLAP `render` extension says when the host renders
  faster than real time. Live input is ignored then, and the synth runs in
  the same `process()` call, so a render matches real-time playback.

**Where it lives:** this repo, as a second target beside `umpire-daw`,
sharing `src/App`. The standalone app stays as it is: it works, and it's
the easiest place to test the clip model and the grid. Its roadmap stops
(below).

## Timing

- **Live input** arrives on CoreMIDI's or ALSA's thread and goes to the
  audio thread by queue. It lands in the next block, at an offset from its
  time stamp. The jitter is up to a block unless the time stamps are used;
  the spike measures it.
- **Recording**: the host's `song_pos_beats` at the block's start, plus the
  offset at the block's tempo, gives the tick.
- **Playback**: each block, the clips are played from the host's position.
  A jump (locate, a loop wrap, the host's cursor moved) is a
  discontinuity: notes are ended and Pitch 7.25 is chased, as playback.md
  does on a seek.
- **Latency**: the plugin reports the synth's latency to the host as its
  own.

## What's given up

- **The host's timeline doesn't show the clips.** The host sees an
  instrument making sound, not MIDI items, so notes are seen and edited in
  the plugin's window, which needs its own view of time beside the grid.
  This is the biggest cost.
- **Notes can't go on to the host's note effects or instruments**: the host
  would round them. Audio effects after the plugin are fine. Note effects
  (Retune, NoteGran, EDOSpread12) would be hosted inside the plugin, before
  the synth: later, a small stack.
- **Two undos.** The host's undo doesn't see edits made in the plugin; the
  plugin keeps its own (`History`).
- **One synth per instance**, so one per host track. Milestone 12's tracks
  over UMP group ranges would happen within an instance, if at all.
- **Sharing the device with the host.** On Linux, a device REAPER opens as
  raw ALSA MIDI may be locked to it; turn it off in the host, or use the
  ALSA sequencer port. CoreMIDI shares.

## The spike (about a day)

A bare CLAP instrument, with no editor beyond a record light and a list of
notes:

- Opens one UMP input, hosts one CLAP synth (PlainOldSynth), and passes
  live notes through.
- Records into one clip while the host records, and plays it back from the
  host's position.
- Saves and restores the clip and the synth's state.

**Checks**, in REAPER and Bitwig (CLAP) and Ardour (the VST3 build), on
the Mac and Linux. The tests stay silent; listening is the author's.

1. Per-note pitch from KeyboardEDO reaches the synth whole, and the delay
   and jitter are acceptable to play with.
2. A recorded take plays back in time with the host's grid, through a tempo
   change.
3. An offline render sounds the same as real-time playback.
4. Saving, closing and reopening the project brings back the take and the
   synth's state.
5. The synth's editor window opens, and Bitwig's plugin sandboxing (its
   hosting modes) doesn't stop the plugin opening the device.
6. Two instances, each with its own synth, both work from one input.
7. In Ardour: the record button reaches the plugin, and the hosted synth's
   timers and file descriptors keep working with the plugin's editor
   closed.

Findings go here. If any of 1 to 5 fails in REAPER and Bitwig without a
workaround, the standalone plan goes on as it was. If only Ardour fails,
the plugin can still go ahead, with the open-source question left open
(Qtractor, Carla, Zrythm next).

## If it works

Proposed milestones, about a day each, as the others:

- **P1. The spike** (above).
- **P2. Recording and playback** on the clip model: takes as clips on the
  plugin's own lane, `Player` driven by the host's position, the chase on
  jumps, latency reported.
- **P3. The editor**: the plugin's window with a lane in time with the host
  and the grid for the selected clip, from `GridCanvas` and `GridPanels`.
- **P4. State and files**: clips as MIDI Clip Files in the state, the
  synth's state, exporting a clip as a file.
- **P5. Host checks**: REAPER, Bitwig and Ardour on both platforms, as
  ReleaseUMP's R3 host checks, and the other open-source hosts (Qtractor,
  Carla, Zrythm).

**What it replaces:** milestones 13 to 18, T1, S1, S2 and the plugin-routing
proposal (R1 to R6). Milestone 12 becomes a question for P2. Zoom (Z1)
carries over to the plugin's lane and grid.

## Open questions

- **The name.** naming.md's rules (no "MIDI"; Umpire) suggest "Umpire" for
  the plugin as well; the standalone app might then become a test bed, not
  a product.
- **The recording UI**: whether the host's record button is enough, or the
  plugin needs an arm of its own (recording in some instances, not others).
- **Hardware output** from the plugin (above): needed or not.
- **Which open-source host to recommend**, once the checks are in (above).
