# Umpire as a plugin

**A proposal, 2026-11-18, on the Mac; not yet decided.** The spike (P1)
is built and checked headless on the Mac. Its checks in hosts, which
decide this, are the author's ("The spike, built", below). What's left of the
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

## The spike, built (P1, 2026-11-18, on the Mac)

Built and checked headless on the Mac. The checks in hosts are still to do,
and they're the author's, since they're listened to. Not built on Linux
yet.

### What's built

- **`src/Plugin/`**: `UmpirePlugin.cpp` is the CLAP entry point and the glue;
  `SpikeEngine` is what runs in `process()`, with no CLAP in it, so the
  tests drive it directly; `PluginSetup` holds the setup file, the choice of
  input, and the state's format. The clip model and MIDI Clip Files come
  from `src/App` through a new library, `umpire-clip` (`Clip`, `ClipFile`,
  `Scale`), which `umpire-app` now links too.
- **Targets**: `umpire-clap` makes `build/Umpire.clap` (CLAP id
  `com.unlikelyware.umpire`, an instrument), and `umpire-vst3` makes
  `build/Umpire.vst3` through clap-wrapper. Both are built as Retune builds
  them: the CLAP entry point is ours, `clap_entry` is the only export, and
  uw-core is linked in with hidden symbols. New submodules
  `external/clap-wrapper` (the fork, `unlikelyware`, at `a436757`) and
  `external/vst3sdk` are pinned as in Retune. The CLAP SDK is uw-core's. No
  AUv2 yet. `install-clap` and `install-vst3` link the bundles into the
  user's plugin folders (BUILDING.md).
- **No editor of its own.** The plugin's editor is the hosted synth's,
  embedded in the host's plugin window: the CLAP gui calls go on to
  `uw::HostedPlugin::openEditor`. So the host's own "show plugin" button
  opens PlainOldSynth's editor, which is check 5. There's no record light;
  the log stands in for one.
- **The setup file** says what to open, since there's no editor to choose
  it in. It lives at `~/Library/Application Support/Umpire/plugin.conf` on
  the Mac, `~/.config/umpire/plugin.conf` on Linux, or wherever
  `UMPIRE_PLUGIN_CONF` points:
  ```
  input = KeyboardEDO      # part of a source's name or id; "none"; left out, it chooses
  synth = PlainOldSynth    # a .clap's name in CLAP's folders, or a path; "none"
  ```
  Left out, the input is KeyboardEDO's if there is one, else the first MIDI
  2.0 source, else the first hardware one. With nothing found it tries again
  every 2 s, and it follows a device that goes and comes back (uw-core's
  `sync()`).
- **The log**: `~/Library/Logs/Umpire/plugin.log` on the Mac,
  `~/.local/state/umpire/plugin.log` on Linux, or wherever
  `UMPIRE_PLUGIN_LOG` points ("none" turns it off). One line per event, each
  with its instance number. It logs the host and what it offers
  (timer-support, posix-fd-support, gui); the synth and the input opened;
  activation, and whether the synth takes MIDI 2.0; "recording" and
  "recording stopped"; each take (notes, length); saves and loads; render
  mode; the editor opening and closing; and every 10 s a heartbeat, with
  the host timer ticks, the main-thread callbacks and the synth's own timer
  ticks since the last one, whether the editor is open, and the live
  input's timing (capture to block: mean, smallest, largest, and how many
  arrived more than a block late). `tail -f` it while checking.

### How it behaves

- **Input**: uw-core's `CoreMidiInput` / `AlsaSeqInput`, a client named
  "Umpire" for each instance. Messages go to the audio thread through a
  `SpscQueue`. Each is placed at a sample offset from its capture time: the
  block is taken to cover the block's length of time just before
  `process()` was called. So the delay is about one block and steady, and
  the jitter is the host's callback jitter, not a whole block. MIDI 1.0 is
  upconverted first, as in the app. The host's MIDI isn't used: the plugin
  has no note ports.
- **The synth** runs in the same `process()`, events at their offsets, in
  MIDI 2.0 if its note port supports it (PlainOldSynth's does; the log says),
  otherwise downconverted. Its transport is the host's.
- **Recording** follows `CLAP_TRANSPORT_IS_RECORDING` (not during pre-roll).
  Ticks come from `song_pos_beats` at the block's start plus the offset at
  the block's tempo. A take replaces the clip; the spike keeps one. The clip
  starts at the song's beat 0, so its ticks are the song's. Notes still held
  when recording stops end there. While recording, the clip doesn't play.
  An offline render never records.
- **Playback**: each block plays the clip's events from the host's position
  to the block's end. A jump is more than half a block away from where the
  last block ended: a locate, a loop wrap, or a new clip. On a jump the
  notes sounding end and Pitch 7.25 is chased, as playback.md does; on stop
  the notes end.
- **State**: `"UMPP"`, a version, the clip as a MIDI Clip File (M2-116-U,
  `src/App/ClipFile.h`), then the synth's id and its own state. The synth's
  state is loaded only if the id matches.
- **Host services**: the synth's `timer-support` and `posix-fd-support`
  requests (through `uw::HostServices`) go on to the host's own; the
  host's `on_timer` and `on_fd` come back to the synth. The plugin also
  registers a 50 ms timer of its own (the heartbeat counts it), and asks
  for a main-thread callback every 50 ms of audio from `process()`. That way
  the synth's main-thread work and finished takes are seen to even on a
  host without timer-support. Instances share one `ClapPluginFactory`, so
  two instances load PlainOldSynth's module once, and the first to close
  doesn't deinit it under the second.
- **Render**: the `render` extension's offline mode drops live input; the
  rest runs as in real time.

### The checks: what's checked here, what's left

Checked headless on the Mac (silent: buffers only, nothing reaches an audio
device):

- **Builds**: `Umpire.clap`, `Umpire.vst3` and `umpire-daw`, no warnings in
  our code. `ninja -C build && ctest --test-dir build --output-on-failure`:
  10 of 10 pass. The new suites are `test-plugin-engine` (the engine with a
  fake synth), `test-plugin-clap` (the built `.clap` through uw-core's CLAP
  host, no synth) and `test-plugin-render` (the built `.clap` hosting the
  installed PlainOldSynth over the CLAP C API; skipped without it).
- **clap-validator 0.4.1**: 20 passed, 0 failed, 24 skipped (those need
  params or note ports, which the plugin doesn't have), both with no synth
  and hosting PlainOldSynth.
- **Steinberg's VST3 validator** (built from `external/vst3sdk` into a
  scratch folder, not part of the build) on `Umpire.vst3` hosting
  PlainOldSynth: 47 passed, 0 failed.

| Check | Checked here | Left for the author, by hand |
|---|---|---|
| 1. Per-note pitch whole; delay and jitter | Rendered through PlainOldSynth, pitch measured: a Pitch 7.25 to 64.5 sounds 338.1 Hz (329.6 untuned), a Pitch 7.9 attribute to 67.5 sounds 402.1 Hz (392.0 untuned). All notes read about 0.3% low against 12-TET A440, the plain ones too, so that's constant. Live messages are placed at offsets from their capture times (test). | Play KeyboardEDO into it in REAPER, Bitwig and Ardour; the heartbeat's "live input" line gives delay and jitter. |
| 2. In time with the grid, through a tempo change | Recording ticks and playback offsets (test); rendered onsets on beat 1 at 120 bpm (frame 24004) and at 90 (32004), 4 frames being the synth's attack. | Record a take and play it back against the host's grid, with a tempo change. |
| 3. Offline render = real time | Hosting PlainOldSynth, a render with the render extension offline matches real time sample for sample. | Render, freeze or bounce in each host and compare with playback. |
| 4. Save, close, reopen | The clip goes in through the state and comes back byte for byte; the synth's id and state are saved. | Save, close and reopen a project in each host; the take and the synth's patch come back. |
| 5. The synth's editor; Bitwig's sandboxing | No window here. | Open the plugin's window (PlainOldSynth's editor) in each host; in Bitwig, try each hosting mode and look for "input:" in the log. |
| 6. Two instances, one input | Two instances host PlainOldSynth at once from one module; when the first closes, the second plays on, the same samples. | Two tracks, each with Umpire, both playing from KeyboardEDO. |
| 7. Ardour: record button; timers with the editor closed | The VST3 builds and validates. The fork's `kRecording` mapping isn't exercised here. | Record in Ardour (a take shows in the log). Then close the plugin's window and watch the heartbeat: do the timer ticks keep coming? (On Linux; IRunLoop is Linux's.) |

Before trying it in a host: run `install-clap` and `install-vst3`, and
write the setup file if KeyboardEDO isn't what you want. Then
`tail -f` the log. **It makes sound** as soon as KeyboardEDO plays into
it, or the host plays a take.

### In hosts

**REAPER 7.79, Mac, CLAP (2026-11-18, the author):**
- **Check 1, pitch: passes.** Non-12-EDO from KeyboardEDO plays correctly
  through PlainOldSynth. The log shows KeyboardEDO's port opened as MIDI
  2.0, and the synth taking MIDI 2.0.
- **Check 1, timing:** at 44.1 kHz and blocks of up to 512 frames
  (11.6 ms), every live message reached the block that played it 0 to
  11.4 ms after capture (mean about 5.5 ms). None was over a block late and
  none was dropped. So the delay is a steady block plus REAPER's output
  latency, not a block of jitter. Whether that's playable is the author's
  call; it wasn't remarked on.
- **Timers:** REAPER gives timer-support but not posix-fd-support (none is
  needed on the Mac). The plugin's own 50 ms timer kept ticking (about 17 a
  second) with the editor open and closed. PlainOldSynth registered a 16 ms
  timer while its editor was open, which REAPER served.
- **Host note effects don't reach it**: EDOSpread12 in front of Umpire in
  REAPER's chain does nothing. That's by design, since Umpire takes no MIDI
  from the host (see "What's given up"). Note effects would have to be
  hosted inside the plugin, before the synth.
- REAPER saves the plugin's state very often (undo points, probably), so
  the log's "saved" lines are noise there.
- **Check 2: passes.** A take recorded with REAPER's record button plays
  back in time with REAPER's grid.
- For a render (check 3) in REAPER, the take isn't a REAPER item, so
  REAPER may see the project as empty: render a time selection that covers
  the take (and its tail).
- Still to try in REAPER: a render (check 3), and save and reopen with a
  take (4).

### Found so far

- **PlainOldSynth keeps Pitch 7.25 only for the sounding note**
  (PlainOldSynth's `src/Module/PitchBendConverterModule.cpp` says so: "NOT
  implemented"). A Pitch 7.25 sent before its note-on is dropped, and the
  next note-on resets it. playback.md assumes "the synths keep it as a
  tuning table", so for PlainOldSynth the chase on a jump does nothing,
  though it does no harm. A Pitch 7.25 sent after its note-on, as
  KeyboardEDO's glide sends it, is recorded and played back in that order,
  and works. A per-key tuning sent ahead of its notes (a tuning table) would
  be lost live as well as on playback. To decide: whether that's the synth's
  to change (the spec makes it persistent) or the chase's to drop.
- **Latency isn't reported.** `uw::HostedPlugin` has no latency, so the
  plugin can't pass on the synth's. P2 needs uw-core to expose it.
- **The transport is read once a block.** A tempo change inside a block (a
  CLAP transport event in the block's events) isn't followed until the next
  block. Fine for the spike; P2 could split blocks at them.
- **Every instance records whenever the host records**, armed or not. CLAP's
  transport says nothing about arming, so the open question below ("an arm
  of its own") is real.
- **Likely in REAPER**: anticipative FX processing renders tracks ahead of
  time, which would delay live input by up to its lookahead. Arming the
  track with monitoring on (REAPER then processes it in real time), or
  turning anticipative processing off for the track, should fix it. To
  confirm in check 1.

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
