# Umpire as a plugin

**Decided 2026-11-18** (decisions.md, "Umpire as a plugin"). Proposed on
the Mac the same day; the spike (P1) was built and checked headless there,
then checked by the author in REAPER and Bitwig on both platforms and in
Ardour on Linux ("In hosts", below), and it passed. P2 (recording and
playback), P3 (the editor) and P4 (state and files) are built on the Mac
("If it works", below). The rest of this section is the proposal as written. What's left of the
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
  `SpikeEngine` (`PluginEngine` since P2) is what runs in `process()`, with no CLAP in it, so the
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
- **Check 3: passes.** File → Render (full-speed offline) reproduces the
  take as it plays in real time. REAPER uses CLAP's render extension: the
  log shows "render: offline", then "render: real time" 0.12 s later. The
  take isn't a REAPER item, so if REAPER sees the project as empty, render
  a time selection that covers the take and its tail. For one track,
  choose "Selected tracks (stems)" as the Render dialog's source.
- **Check 4: passes.** After saving, closing and reopening the project,
  the take plays again. The log's "loaded: clip of 23 events to beat 21.02;
  synth state loaded" is the take recorded before.
- Check 5 in part: PlainOldSynth's editor opens as the plugin's window
  (the log shows it opening and closing). Bitwig's sandboxing is for Bitwig.
- Still to try in REAPER: two instances (check 6). Check 6 passed in
  Bitwig (below).

**Bitwig Studio 6.1.4, Mac, CLAP (2026-11-18, the author):**
- **Check 5: passes.** PlainOldSynth's editor opens as the plugin's window,
  and the plugin opens KeyboardEDO's port from inside Bitwig's plugin host
  process. It did both times the project was opened: two instances sharing
  one process, then a fresh process when the project was reopened (check 4).
  The log doesn't say which hosting modes were tried.
- **Check 6: passes.** Two instances, each with its own PlainOldSynth, both
  play from KeyboardEDO at once. Each logs the same live input (60
  messages per 10 s), all within a block, none dropped.
- **Bitwig gives no timer-support on the Mac** (and no posix-fd-support):
  the heartbeat shows 0 timer ticks and 173 main-thread callbacks per
  10 s. The main-thread callback that `process()` asks for every 50 ms is
  what keeps the plugin's main-thread work going there (finished takes,
  the synth's callbacks). PlainOldSynth's editor doesn't need host timers
  on the Mac.
- **Check 4: passes.** Saved, closed and reopened, both instances came back
  with their take and the synth's state ("loaded: clip of 28 events to beat
  16.96; synth state loaded"). Both had the same take: recording in Bitwig
  recorded on both tracks at once, which is the finding below that every
  instance records whenever the host records.
- A take recorded, and a render went through the render extension. Bitwig
  deactivates and reactivates the plugin around each change of render mode.
- Live timing as in REAPER: 0 to 11.6 ms from capture to block at 44.1 kHz
  and 512 frames, never over a block late.
- The browser lists Umpire twice, the CLAP and the VST3; the FORMAT column
  tells them apart.

**REAPER 7.85, Linux, CLAP (2026-11-18, the author):**
- **The host records its own copy.** With the track armed and its record
  input on MIDI, REAPER records KeyboardEDO into a MIDI item of its own
  while the plugin records its take. The item is never played (the plugin
  has no note ports), so editing it, as the author did by dragging its
  notes, changes nothing that sounds. Record mode "Record: disable (input
  monitoring only)", from the arm button's right-click menu, keeps the
  track armed and monitored and makes no item; that works, but it's awkward.
- **Live delay was the block size.** REAPER's audio system was JACK, which
  here is PipeWire's, so PipeWire's quantum (1024 frames at 48 kHz, 21 ms)
  set the block, not REAPER's own block size (128, used only in its ALSA
  mode). The delay was noticeable. Starting REAPER with
  `pw-jack -p 256 <path to reaper>` (or adding `-p 256` to the launcher's
  `Exec=` line) gives blocks of 256: capture to block 0.2 to 4.8 ms, and
  the delay is gone (the author).
- **Once, a take played back seconds late**, after switching to "Record:
  disable". Not reproduced in either record mode afterwards. The take's log
  line now gives its start, its first note-on and the tempo, so if it comes
  back the log tells whether the take was stored late or played late.

**Bitwig Studio 6.1.7, Linux, CLAP (2026-11-18, the author):**
- The same as REAPER: an armed track records Bitwig's own clip as well as
  the plugin's take. With the track's record arm off, only the plugin
  records (its recording follows the transport, not the arm). The author
  will live with that for now, with a release note (below).
- Takes recorded where played ("first note-on at beat 4.02" for a note on
  bar 2 at 110 bpm). Blocks of up to 1024 frames; capture to block 0 to
  20.9 ms.
- Bitwig gives timer-support on Linux (about 20 ticks a second), unlike on
  the Mac.

**Ardour 8.4 (Ubuntu 24.04's package), Linux, VST3 (2026-11-18, the author):**
- Ubuntu's `ardour` package is the full program (ardour.org's free binary
  is a demo). Ardour 8 doesn't host CLAP, so it's the VST3 (the log says
  "in Ardour (CLAP-as-VST3)"). Ardour found new VST3s but didn't scan them
  until asked (Window → Plugin Manager, or Preferences → Plugins).
- **Check 7, timers: none with the plugin's window never opened.** Ardour
  offers timer-support and posix-fd-support (through clap-wrapper, from
  VST3's `IRunLoop`), but before the window was first opened every
  heartbeat read 0 timer ticks, 0 main-thread callbacks, 0 synth timer
  ticks, and heartbeats only came when Ardour saved the plugin's state
  (about every 2 minutes; `saveState()` runs the main-thread work). So a
  finished take went in only at the next save, and "recording" was never
  logged. Once the window opened: about 198 of the plugin's 50 ms timer
  ticks, 157 main-thread callbacks and 327 of PlainOldSynth's 16 ms timer
  ticks per 10 s. **Closing the window doesn't stop them**: Ardour hides it
  and calls the plugin's `gui.destroy` only when the session closes, so the
  log still says "editor open" and the ticks go on. So opening the window
  once per session is enough; after reopening the session there are none
  again until it's opened. P2 can't rely on it: the main-thread work needs another way in
  when the host gives no run loop (for instance from `process()` through
  a lock-free hand-off, or doing the take's hand-over on the audio thread).
- **Check 2: passes in part.** Ardour records only with an armed track, so
  it records a MIDI region of its own on Umpire's track as well. Takes land
  where played ("first note-on at beat 4.07"). Recording starts at beat
  -0.08 (Ardour's transport starts a little before the playhead).
- **Check 3: passes.** Session → Export with Stem Export, over a range
  selection (the take isn't a region, so Ardour's session range may be
  empty), then imported back: the audio is in time with the take. Exporting
  reactivates the plugin with blocks of up to 8192; there's no render-mode
  change in the log (the VST3's offline mode doesn't reach the render
  extension, or Ardour exports in real time; to look at).
- Live input feels in time to the author at 1024-frame blocks (Ardour's
  ALSA backend, 21.3 ms).
- **Ardour's own instruments need its MIDI input connected**: a second
  track with StruckSynth heard nothing from KeyboardEDO. Ardour was on its
  ALSA backend, which sees a software client such as KeyboardEDO only with
  its MIDI system set to the ALSA sequencer; then the track's input is
  chosen. Not confirmed. Through Ardour it would be 12-EDO in any case
  (MIDI 1.0).
- **Check 4: passes.** Saved, quit and reopened: the take and the synth's
  state came back ("loaded: clip of 12 events to beat 15.10; synth state
  loaded") and play.
- **Check 6: passes.** Two Umpire tracks, each with its own PlainOldSynth,
  both play from KeyboardEDO, non-12-EDO layouts included; both instances
  log the live input, none late or dropped.

**Ardour on the Mac: not tried.** The author's Ardour on the Mac isn't an
arm64 build, and Umpire is built for arm64 only (as StageHand and Retune
are), so it can't load the plugin. Ardour, check 7, waits for Linux, where
the timer question belongs anyway.

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
  **Decided 2026-11-19: the synth's.** PlainOldSynth now keeps Pitch 7.25
  per key (group, channel, note) until a new one or a Per-Note Management
  Reset, and sends it again after each plain note-on of the key; a note-on
  with a Pitch 7.9 attribute keeps its own pitch, and a CLAP tuning
  expression still lasts for its note only. The chase is unchanged, and now
  works: test-plugin-render plays from past a key's Pitch 7.25 and hears its
  note tuned. The other synths sharing the module don't keep it yet.
- **Latency isn't reported.** `uw::HostedPlugin` has no latency, so the
  plugin can't pass on the synth's. P2 needs uw-core to expose it.
- **The transport is read once a block.** A tempo change inside a block (a
  CLAP transport event in the block's events) isn't followed until the next
  block. Fine for the spike; P2 could split blocks at them.
- **Every instance records whenever the host records**, armed or not. CLAP's
  transport says nothing about arming, so the open question below ("an arm
  of its own") is real. **Fixed 2026-11-19** (P2, "The arm"): a Record
  parameter.
- **Likely in REAPER**: anticipative FX processing renders tracks ahead of
  time, which would delay live input by up to its lookahead. Arming the
  track with monitoring on (REAPER then processes it in real time), or
  turning anticipative processing off for the track, should fix it. Not
  seen on Linux: armed and monitored, the delay there was the block size
  (above).

### For the release notes

- **Umpire records while the host records and its Record parameter is
  on** (off when it's added). Turn it on in the host's view of the plugin's
  parameters, or automate it. In REAPER: the FX window's Param button,
  "Show in track controls", Record; then drag or scroll the slider on the
  track header past half. Its takes are kept inside the plugin and
  saved with the project; the host's timeline doesn't show them.
- **Keep the host from recording a clip of its own** on Umpire's track: in
  Bitwig, turn off the track's record arm; in REAPER, leave it armed with
  monitoring on and set its record mode to "Record: disable (input
  monitoring only)" (an unarmed REAPER track may be rendered ahead,
  delaying live input). Ardour records only with an armed track, so there
  it does record one: ignore or delete it. The host's clip plays nothing,
  and editing it changes nothing.
- **To record other tracks while Umpire plays** (printing its sound to an
  audio track, say), leave its Record off: it plays its takes as usual.
- **In Ardour, open Umpire's window once each session if the synth needs
  timers** (since P3 that's Umpire's own window; the synth's opens from it). Until it's opened, Ardour gives the plugin no timers, so a
  synth that needs them stalls; closing it afterwards is fine. Takes no
  longer wait for it (P2: the lane thread). A device that goes away and
  comes back is reopened only once the window has been opened.
- **Host note effects in front of Umpire do nothing**: Umpire takes its
  notes from the device, not the host.
- **Umpire's window** (P3) shows its takes and edits them. Its Undo (Edit,
  or Cmd+Z when the window has the keys) is its own; the host's undo
  doesn't undo edits made in it. Hosts may keep keys for themselves: in
  REAPER, turn on "Send all keyboard input to plugin" for the grid's keys,
  or use the Edit menu and right-click. The synth's editor opens from the
  button with its name, in a window of its own.
- **Choosing the synth and the input** (P4): the ▾ beside each in Umpire's
  header. The choice is saved with the project, so each track can have its
  own synth; `plugin.conf` only sets what a new instance starts with. The
  first time the synth menu opens, Umpire looks at every CLAP plugin
  installed, which takes a moment. A project's synth that isn't installed
  shows as "not installed": nothing sounds, and its patch is kept.
- **Takes as files** (P4): Edit > Export take... saves a take as a MIDI
  Clip File (`.midi2`); Import clip... puts one on the lane at the playhead.
- **On Linux with PipeWire**, the block size comes from PipeWire, not the
  host: start the host with `pw-jack -p 256` (or set PipeWire's quantum)
  for a short delay between playing and hearing.

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

### P2: recording and playback (built 2026-11-18, on the Mac)

What the spike left, from its host checks ("Found so far" above), and the
milestone's own list. Still no editor of its own (that's P3), so the log
still stands in for one.

- **Takes are clips on the plugin's lane.** Each take becomes a clip (a
  `ClipRef`: where it starts, its events from there), named "Take n". A
  take with nothing in it leaves nothing, as in the app (recording.md).
  The lane keeps every take.
- **The newest take plays where takes overlap; older ones are kept
  underneath**, not cut. So recording over a passage replaces it in what's
  heard, and nothing is lost: P3's editor can show the older takes and bring
  one back. What plays is worked out on a non-audio thread whenever the lane
  changes: each take's visible pieces are its span less the spans of newer
  takes. A note sounding where a piece ends gets a note-off there; a note
  that began under a newer take isn't started where its piece resumes. At
  the start of each piece after the first, each key the take tunes gets its
  Pitch 7.25 in force there (or a reset if none yet).
- **A jump while recording ends the take and starts another** from where
  the host now is, as the app does on a locate (recording.md). So recording
  in a loop gives a take for each pass, the last one heard, the others
  underneath: loop recording, newest wins.
- **Takes reach the lane without the host's main thread.** Ardour gives
  the plugin no timers and no main-thread callbacks until its window has
  opened (check 7), so a take waited for the next save. Each instance now
  has a thread of its own, the lane thread, which every 10 ms collects
  finished takes from the engine, adds them to the lane, and gives the
  audio thread what to play. The engine has two take buffers, so a new
  take (the next loop pass) starts at once while the last waits to be
  collected. The main thread touches the lane only under the lane's
  mutex (state save and load, later the editor), never the audio thread.
  Marking the host's state dirty is still the main thread's (CLAP says so):
  the lane thread sets a flag, and the main thread's next housekeeping
  marks it. Ardour saves now and then in any case.
- **Tempo changes inside a block are followed.** CLAP transport events in
  the block's input events split it: each piece is played and recorded at
  its own position and tempo. The synth still runs once for the whole
  block.
- **Latency is reported**: the synth's, as the plugin's own (CLAP
  `latency`). uw-core's `HostedPlugin` gains `latencyFrames()` (CLAP
  `latency` on the hosted plugin), and `HostServices::latencyChanged()`.
  When the synth reports a change while it's being activated, the plugin
  tells the host (allowed while activating); otherwise the synth has asked
  for a restart, which goes on to the host as before.
- **The state, version 2**: "UMPP", 2, the number of takes, then for each
  its start (i64 ticks) and its MIDI Clip File; then the synth as before.
  Version 1 (the spike's one clip from beat 0) still loads, as one take at
  0.
- **`SpikeEngine` becomes `PluginEngine`**: it's no longer a spike.

**The arm (decided and built 2026-11-19).** Without one, every instance
recorded whenever the host recorded, armed or not, and the newest take
wins: playing into Umpire while recording another track, or two instances
on one input, would cover the take you meant to keep. So the plugin has one
CLAP parameter, **Record** (id 1, stepped, off by default, as a host's
tracks are; automatable). A take records only while the host records *and*
Record is on:
- Turning it on while the host records punches in there; turning it off
  ends the take there. A change in the block's events splits the block at
  its frame, as a tempo change does, and the position goes on from the
  piece before it.
- An instance that isn't armed plays its lane while the host records,
  and its live input still plays through, so recording other tracks with
  Umpire loaded is fine now.
- Hosts show it in their own parameter views and automation lanes
  (REAPER's FX parameters, Bitwig's Remote Controls or the device's
  parameter list, Ardour's generic controls); P3's editor can put a button
  on it. clap-wrapper maps it to a VST3 parameter.
- The state (version 3) keeps it: flags after the synth's state, bit 0
  armed. Versions 1 and 2 load unarmed.
- The log says "record: armed" and "record: off"; "saved" and "loaded"
  name it.
- Checked headless: `test-plugin-engine`'s testArm (unarmed: no take, the
  lane plays on, live input plays through; armed at frame 240 of a block:
  the take starts there, its note at its tick; disarmed: the take ends), the
  state's version 3 and version 2; `test-plugin-render` through CLAP (one
  parameter, Record, stepped, off; flush arms it; the state keeps and loads
  it). Steinberg's VST3 validator: 47 passed, 0 failed, with no synth and
  with PlainOldSynth. clap-validator 0.4.1: 30 passed, 2 failed, 12
  skipped (both ways). The two failures are `param-set-events` and
  `param-set-no-cookies`: they set each parameter to a random value and
  expect something to change, and for Record they drew 0, its default (seen
  with a probe), so nothing could. Expected with one on/off parameter that
  defaults to off; the flush path itself is checked above.

**Not in P2:**
- **Milestone 12's question** (tracks over UMP group ranges): no. One
  instance plays everything from its input to its one synth; a second
  synth is a second instance on another host track (check 6).
- **The chase and PlainOldSynth's Pitch 7.25** ("Found so far"): the chase
  is unchanged; the synth changed (decided 2026-11-19).
- **The input's retries and device changes** (`openInput()`, `sync()`) stay
  on the main thread, so in Ardour before the window opens a device that
  comes back isn't reopened. The input opens at `init()` either way.

**Built** on the Mac the same day, and checked headless; the checks in
hosts are the author's (below).

- **New files**: `src/Plugin/Lane.{h,cpp}` (the takes, and `flatten()`:
  what plays), `src/Plugin/LaneKeeper.{h,cpp}` (the lane thread), and
  `PluginEngine` in place of `SpikeEngine`. uw-core `df496de`:
  `HostedPlugin::latencyFrames()`, `HostServices::latencyChanged()`.
- **While this instance records, its lane is silent**, as the spike's clip
  was: older takes don't sound under the one being recorded. Before a punch-in
  (the host's record flag off, or Record off) the lane plays as usual.
- **The log** names each take ("Take 3: 5 notes, ... from beat 8.00 to beat
  12.00, first note-on at ..."), says "latency: n frames (the synth's)" when
  it changes, "saved: n takes ..." and "loaded: n takes ...", and "take(s)
  not recorded" if both take buffers were still waiting (not seen in the
  tests unless the lane thread is stopped).
- **Checked headless** (silent): `ninja -C build && ctest --test-dir build
  --output-on-failure`, 10 of 10. `test-plugin-engine` adds: a loop
  recorded as a take per pass, oldest collected first, a held note ended
  where its pass did, a third pass missed while neither buffer was
  collected; deactivating mid-take ends it; a tempo change at frame 240 of a
  block played and recorded at the new tempo; `flatten()` with an older take
  cut by a newer one (the cut note ended, the covered note-off dropped, the
  tuning chased where the older resumes, a piece's end before the next
  piece's start at one tick), a take wholly covered, takes back to back; the
  lane thread collecting a take with nobody calling it, and a loaded lane
  played. `test-plugin-clap` loads and saves two takes through the CLAP
  state; `test-plugin-render` as before, through version 2. uw-core's
  tests, 9 of 9, read StruckSynth's and Retune's latency (0). clap-validator
  0.4.1: 20 passed, 0 failed, 24 skipped, with no synth and with
  PlainOldSynth, as in P1. Steinberg's VST3 validator wasn't run again.

**Checks in hosts (the author's, by ear):**

1. **Takes keep**: record twice in different places; both play back; save,
   close and reopen; both come back ("loaded: 2 takes").
2. **Newest wins**: record over the middle of an earlier take; the new one
   plays there, the old one before and after it, with no stuck or cut-off
   notes at the joins.
3. **Loop recording**: loop a bar and record three passes; the last pass
   plays; the log has a take per pass.
4. **Ardour, the window never opened**: a take plays back as soon as
   recording stops (before, it waited for Ardour's next save). The log's
   "Take n" line comes at once.
5. **A tempo change while playing** a take (REAPER's tempo envelope, say):
   still in time with the grid.
6. **Latency**: the log's "latency" line, if the synth has any
   (PlainOldSynth probably reports none, so this mostly checks nothing
   breaks).
7. **The arm** (2026-11-19): with Record off, the host recording makes no
   take and the lane plays on; turn Record on in the host's parameter view
   and record: a take. Two instances on one input, one armed: only it
   records. Record comes back on after save and reopen. In Ardour, through
   the VST3: the parameter shows in its generic controls. *REAPER on the
   Mac, 2026-11-19 (the author): works. With Record off, REAPER's own
   recording made no take; Record is reached from the FX window's **Param**
   button, "Show in track controls", which puts it on the track header as a
   slider (drag or scroll it past half for On), or the **UI** button
   (REAPER's own parameter list in place of the synth's editor).*
8. **PlainOldSynth's tuning table** (2026-11-19, PlainOldSynth rebuilt):
   KeyboardEDO resets each key before each note-on (its Per-Note Management
   option, on by default), so its takes sound as before: play a take with
   glides from the start and from the middle, and loop it. With that option
   off, a key keeps its last glide for its next note, as MIDI 2.0 says.

### P3: the editor (built 2026-11-19, on the Mac)

The plugin's own window, in place of the synth's editor, which until now
was what the host's plugin window showed. The design was proposed with a
mockup and agreed with the author before it was built (decisions.md, "The
plugin's window"; ui-direction.md's rules: only what you need, hide
controls but never state, nothing changes on a guess).

```
┌ Record │ Edit │ PlainOldSynth                         KeyboardEDO   9.2.1 ┐  header (ImGui's menu bar)
│ 1        2        3        4        5        6        7  ...             │  the ruler: the host's bars
│ [Take 1 ▪▪▪▪▪▪][Take 3 ▪▪▪▪▪]    [Take 2 ▪▪▪]       │ playhead          │  what's heard, newest on top
│          [░░ Take 1, covered ░░]                                         │  the strip: covered parts
├──────────────────────────────────────────────────────────────────────────┤
│ keys │ the selected take's grid, in song time, the playhead through it │ inspector │
└──────────────────────────────────────────────────────────────────────────┘
```

**Decided with the author (2026-11-19):**
- **The host's plugin window is Umpire's.** The synth's editor opens in a
  window of its own (uw-core's `EditorWindow`, as "What carries over"
  planned), from a button with the synth's name in Umpire's header, lit
  while that window is open. Closing Umpire's window closes it too.
- **Record** is a button in the header (red when on), bound to the Record
  parameter (id 1). A press is applied by the audio thread at the next
  block, or by `params.flush` if the host isn't processing (the plugin asks
  for one), and goes to the host as a gesture (begin, value, end), so the
  host records it as automation and its own view follows. A change from the
  host shows on the button.
- **The lane** shows the takes in the host's bars (4/4: the grid's bars are
  sixteen sixteenths), each with a sketch of its notes. Where takes
  overlap, what's heard is drawn: the newest. The parts of older takes that
  newer ones cover show as a thin strip under them, so nothing that changes
  what's heard is hidden. A click on a take chooses it for the grid; a
  click on the strip chooses the newest covered take there, and another
  click the next older. A click on empty lane chooses none, and the grid
  goes. With no takes the lane says "Turn on Record and record in the host
  to make a take."
- **Bring take to front** (Edit, and a take's right-click menu) makes an
  older take the newest, so it's heard where it overlaps others. **Delete
  take** is beside it.
- **Undo is the plugin's own** and covers the lane: a take recorded, a take
  deleted or brought to the front, and every edit in the grid, named as the
  app names them ("Undo Move note"). Edit has Undo and Redo; Cmd+Z and
  Cmd+Shift+Z (Ctrl on Linux) work when the window has the keys. The host's
  undo doesn't see these (above, "Two undos"). A loaded state starts a new
  history.
- **While the host plays**, the lane and the grid page to keep the playhead
  in view, until scrolled by hand (Shift+wheel or a sideways swipe); the
  next play starts paging again. No zoom (Z1 stays separate).

**How it's built:**
- **The window** (`src/Plugin/PluginEditor.{h,cpp}`) is DGL embedded in
  the host's view (DGL's embed `Window` and its own `Application`, not
  DPF's plugin wrapper), its top-level widget the app's `GridCanvas`, with
  `GridPanels` over it. The grid code now lives in two libraries both
  targets share: `umpire-clip` gains `ClipEdit` and `GridModel` (they need
  only `uw::ump`), and `umpire-grid-ui` holds `GridCanvas`, `GridPanels`,
  `ImGuiWidget` and `Widgets`. The app's grid windows are unchanged: the
  new hooks default to what they did.
  - `GridCanvas` takes a **band** (`GridBand`) between the menu bar and
    the grid: the plugin's lane. It draws it, routes its clicks, wheel and
    motion to it, hides the grid while no take is chosen, and draws the
    playhead on the grid and pages to it.
  - `GridPanels` takes **hooks**: no File or View menu, the Record button
    first in the menu bar, the take's commands at the end of Edit, the
    synth's button and the status at the right, the right-click menu, no
    inspector without a take, and the name read only (a take's name is its
    number until P4 saves names).
  - `GridModel` has an **origin**: the grid is in song time from the bar
    its take starts in, so its columns, bar lines, Ctrl-drag and Quantize
    are the host's sixteenths even when a take starts off the beat
    (Ardour's start at beat -0.08, a locate mid-bar). What it gives the
    lane is in the clip's ticks; a note moved before the take's start
    starts the take earlier (`LaneKeeper::editTake` shifts the clip).
- **Edits go back through the lane.** `LaneKeeper` gains `editTake`,
  `deleteTake`, `bringToFront`, `undo`, `redo` and `view()`, all under its
  lock on the main thread; `Lane` gains `replace`, `remove`,
  `bringToFront`, `restore` and `visibleSpans` (what the lane draws as
  heard). Each change gives the engine new play data through its queue, as
  a take does, so the audio thread still neither locks nor allocates, and
  sets the flag the main thread turns into the host's `mark_dirty`. A take
  recorded is an undo step ("Record take").
- **The host's position** for the playhead: `PluginEngine::position()` and
  `hostPlaying()`, atomics the audio thread writes at the end of each block.
- **Timers**: the window is driven at about 60 a second by the host's
  `timer-support`, or, where the host has none (Bitwig on the Mac), by a
  run-loop timer of its own in the common modes, as Retune's editor. On the
  Mac its size is in points, scaled by the backing scale of the view it's
  in, read live (`PluginEditorMac.mm`) since DGL reads it once; moved to a
  screen of another scale, it's sized again.
- **The log** says "window opened (960x600 at 2.0)", "window closed",
  "synth's window opened" and "closed"; the heartbeat says whether each is
  open.

**Checked on the Mac (headless, silent):**
- `ninja -C build && ctest --test-dir build --output-on-failure`: 10 of 10.
  `test-plugin-engine` adds testEditor: a note moved in the grid goes into
  the take through `editTake` (one "Move note" step, the take's id kept,
  the state dirty) and is heard at its new beat; a note moved before the
  take starts the take earlier; undo and redo of it; bring to front and
  delete, and undo of the delete; a recorded take as a "Record take" step;
  the covered spans; the grid's origin (a note at its song beat); the
  host's position. `test-plugin-clap`: the plugin now has a window without
  a synth.
- **`tools/PluginWindowCheck.mm`** (`ninja -C build
  umpire-plugin-window-check`; not a ctest, since it opens a window): a bare
  Cocoa host with no timer-support (as Bitwig on the Mac) opens the built
  Umpire.clap's window with three overlapping takes, plays at 120, and
  clicks into it itself. Passed: the window opens; a click on Take 1 shows
  its grid; the Record button sends the host a gesture with On, and Record
  is on; a click on the grid adds a note to the take (17 notes in the saved
  state); Cmd+Z takes it out (16). Its frames (`UMPIRE_PLUGIN_CAPTURE`,
  since `screencapture` needs the Screen Recording permission) show the
  lane, the covered strip under Take 2, the paging playhead in the lane and
  the grid, Record lit, and the inspector.
- **clap-validator 0.4.1**: 30 passed, 2 failed, 12 skipped, with no synth
  and with PlainOldSynth: the same two expected `param-set-*` failures as
  P2 ("The arm"). It doesn't open the window.
- **Steinberg's VST3 validator** (built from `external/vst3sdk` into a
  scratch folder): 47 passed, 0 failed, with no synth and with PlainOldSynth.
- `build/umpire-daw --self-test`: the track view's checks pass; the grid
  window's input checks fail from "click on the grid goes to the canvas"
  on, **the same with the code before P3** (built and run to compare), so
  that's older than P3 and Mac-only as far as known (clip-editor.md's run
  was on Linux). Not looked into here.

**Checks in hosts (the author's, by eye; the window makes no sound, but a
take plays when the host plays):**

1. **The window opens** in REAPER and Bitwig (CLAP) on the Mac, and in
   Ardour (VST3) on Linux (Ardour on the Mac waits for an arm64 build): the
   header, the lane with the takes, "window opened" in the log. Resize it.
2. **It follows the host**: the playhead moves with the host's, in the lane
   and the grid, pages while playing, and stops when the host stops; a
   tempo change keeps it on the host's grid.
3. **Record**: the button lights the host's Record parameter and back; host
   automation of Record lights the button; recording with it on makes a take
   that appears in the lane as soon as recording stops.
4. **Edit a take**: click it, move, add and delete notes in the grid; play:
   the edit is heard. Edit > Undo and Redo, with their names. Save, close
   and reopen the project: the edit is kept.
5. **Older takes**: record over part of a take; the covered part shows as a
   strip; click it, Bring take to front: it's heard again; undo.
6. **The synth's window**: the synth's button opens PlainOldSynth's editor
   in a window of its own, and closes it; closing that window unlights the
   button. In Bitwig, try it with each hosting mode.
7. **Keys** in each host: Delete, the arrows, Q, and Cmd+Z in the grid. A
   host may keep them (Cmd+Z is REAPER's and Bitwig's undo); REAPER passes
   them with "Send all keyboard input to plugin" (the FX window's menu).
   The Edit menu and right-click do everything without keys.
8. **Two instances**, each with its window open: each shows its own lane.

**Not in P3:**
- **Names and scales of takes** aren't saved (P4: state and files), so the
  grid's name field is read only and its tuning is the window's, worked out
  again for each take. *Done in P4.*
- **Meters other than 4/4**: the grid's bars are sixteen sixteenths, and
  the lane's follow them.
- **Choosing the input and the synth** in the window: still the setup file.
  *Done in P4.*
- **Linux**: not built there yet; the X11 side (`set_scale`, sizes in
  pixels) is written but untried.

### P4: state and files (built 2026-11-19, on the Mac)

The takes and the synth's state were already in the plugin's state (the
takes as MIDI Clip Files since P1 and P2), so P4 is what P3 left. The scope
was agreed with the author before it was built (decisions.md, "State and
files"): all four of the candidates, the choices kept in the project, and a
synth change undoable with its patch.

```
┌ Record │ Edit │ PlainOldSynth ▾                       KeyboardEDO ▾   9.2.1 ┐
                                 ┌──────────────────┐  ┌───────────────────────┐
                                 │ PlainOldSynth  ✓ │  │ Default (KeyboardEDO) │
                                 │ StruckSynth      │  │───────────────────────│
                                 │ Surge XT         │  │ KeyboardEDO         ✓ │
                                 │──────────────────│  │ KeyLab 88             │
                                 │ None             │  │───────────────────────│
                                 └──────────────────┘  │ None                  │
                                                       └───────────────────────┘
Edit: Undo · Redo · ─ · Bring take to front · Delete take · ─ · Export take... · Import clip...
Inspector: Name [Verse riff]   Tuning: EDO 31, step 0 at 57, "Set by hand"
```

**Decided with the author (2026-11-19):**
- **Takes keep their names and scales.** The take's name is renamed in a
  native text box ("Rename take", below) and the tuning sliders set the
  take's scale ("Set scale", shown as "Set by hand"), each an undo step, as
  in the app.
- **Rename take... is a native text box** (decided 2026-11-19, after the
  author's first try): clicking the inspector's name, its "Rename..."
  button, or Edit > Rename take... opens a sheet with a text field on the
  host's window on the Mac (zenity or kdialog's entry on Linux). Editing
  the name in the inspector's ImGui field lost its keys to the host: in
  REAPER, Backspace went to REAPER, which deleted the selected effect,
  Umpire itself (the log showed the instance closed, then remade by
  REAPER's undo). Hosts pass keys to a real text field, so the box gets
  them. A click in Umpire's window now also takes the keyboard focus (the
  view becomes first responder), which helps the grid's keys reach it. A
  take's scale is worked out from its notes when it's recorded or loaded,
  unless it was set by hand. The next take's number is kept too, so a
  deleted take's name isn't given again after a reload.
- **Export take... and Import clip...**, in Edit and in a take's
  right-click menu. Export writes the take as a MIDI Clip File (M2-116-U,
  files.md), from the take's own start, with the host's tempo and meter
  for other apps; it suggests the take's name. Import puts a `.midi2` on
  the lane as the newest take, at the host's playhead (where it stands
  when stopped), named after the file, one undo step ("Import clip"), and
  chooses it for the grid. Both start in the last folder used, else
  `~/Music`.
- **The synth is chosen per instance**, from a menu (▾) beside the synth's
  button: the CLAP instruments in CLAP's folders, and None. So two tracks
  can host different synths; before, every instance hosted `plugin.conf`'s.
  **"Change synth" is undoable** with its patch: undo brings back the old
  synth with the state it had.
- **The input is chosen per instance**, from a menu (▾) beside its name:
  Default (the setup file's, or the automatic choice, named with what it
  opens), each MIDI source, and None. Not an undo step (it changes no
  music), but it marks the project changed.
- **Both choices are kept in the project** (the plugin's state). A new
  instance starts from `plugin.conf` as before. **A synth or input that
  isn't on this machine is kept, not replaced**: the header shows "Surge XT
  not installed" (amber; nothing sounds, and saving keeps its patch as it
  was) or "KeyLab 88 (not here)" (it opens when it comes). This also fixes
  P1's loss: a project opened where its synth isn't installed (or isn't
  the setup file's) used to drop the synth's state at the next save.

**How it's built:**
- **The state, version 4** (`PluginSetup.h` has the layout): each take
  adds its name, its scale's EDO and reference note, and how the scale was
  arrived at (only "set by hand" is used on load; the rest is worked out
  again); then the next take's number; the flags gain "the synth was
  chosen"; and at the end the synth's `.clap` path and name, and the input
  (how it's chosen, its name and its id). Versions 1 to 3 still load: their
  takes are named "Take n", a synth named by id counts as chosen, and none
  named is the setup file's.
- **Finding a saved synth** looks only where it's cheap: the saved `.clap`
  path, a `.clap` of that file's name or the synth's name in CLAP's
  folders, and the setup file's synth (an older state has only the id).
  Scanning every installed CLAP loads each one into the host's process, so
  only the synth menu does that, once per process, the first time it's
  opened (it can take a moment). A synth found by that scan is found by id
  from then on.
- **The synth is swapped while the host plays, lock-free.** On the main
  thread the new synth is made, given its state, and activated; the engine
  (`PluginEngine::swapSynth`) hands it to the audio thread through a
  queue. At its next block the audio thread stops the old one
  (`stop_processing`, which CLAP says is the audio thread's), plays the new
  one, and hands the old back; the main thread deactivates and destroys it
  (`retiredSynth()`, from housekeeping and deactivate). The lane goes on in
  the new synth from where it is, its tuning chased, as after a jump.
  Inactive, the swap is immediate. If the host has stopped processing
  (asleep), the plugin asks it to process (`request_process`); four swaps
  can wait, a fifth is refused with a log line. A new latency asks the
  host for a restart, as before.
- **Undo across the lane and the synth**: `LaneKeeper` takes steps outside
  the lane (`OtherStep`): each undo or redo swaps back what it holds and
  returns the reverse. It runs without the lane's lock, since swapping a
  synth calls the host.
- **Files**: the app's `FileDialog` (AppKit's panels on the Mac, zenity or
  kdialog on Linux) is compiled into the plugin, and gains a text entry
  (`Mode::Text`: an NSAlert with a text field, or zenity's or kdialog's
  entry) for renaming. On the Mac the panel is a
  sheet on the host's window that holds Umpire's view
  (`FileDialog::setParent`): begun as a window of its own, it opened behind
  Umpire's window (the author, 2026-11-19), since a host may float its
  plugin windows above others. The app's panels are as before. On Linux, where the tools
  don't ask, Export asks "Replace it?" itself. Problems (no dialog tool, a
  file that isn't a MIDI Clip File) are said in a prompt with OK.
- **The log** says "synth: ..." or "synth X isn't here", "input chosen:
  ...", and "saved:" and "loaded:" name the synth.

**Checked on the Mac (headless, silent):**
- `ninja -C build && ctest --test-dir build --output-on-failure`: 10 of 10.
  `test-plugin-engine` adds: names kept on load, numbering on from the
  highest "Take n"; rename and set scale (clamped) and their undo; import as
  the newest take, named, its scale worked out, heard, undone; state
  version 4 round-tripped, cut short at every 7th byte and refused, a scale
  out of range not kept, version 3 read; a step outside the lane undone and
  redone among lane steps; the synth swapped inactive (at once) and active
  (the audio thread stops the old one and plays the new; it comes back
  deactivated), four swaps waiting and a fifth refused, deactivating with
  swaps waiting, and None. `test-plugin-clap`: names, scales and the next
  number through the plugin's state; a synth that isn't installed and an
  input that isn't here kept as they were. `test-plugin-render`: a state
  choosing no synth is silent; one choosing PlainOldSynth, loaded while the
  plugin runs, swaps it in and it sounds; one choosing a synth that isn't
  installed is silent and saves it back unchanged.
- **clap-validator 0.4.1**: 30 passed, 2 failed, 12 skipped, with no synth
  and with PlainOldSynth: the same two expected `param-set-*` failures.
- **Steinberg's VST3 validator**: 47 passed, 0 failed, both ways.
- **`tools/PluginWindowCheck.mm`**: its window opened and drew the new
  header (the ▾ menus, "No synth", "No input"), but none of its clicks
  landed, so its Record, grid and undo checks failed. P3's commit, built
  and run the same day, failed the same way, so it's the machine, not P4:
  the Mac was in use, and the check's window didn't get the keys. To run
  again with the Mac otherwise idle. The menus, rename and the dialogs
  aren't in it (the synth menu would scan every installed plugin).

**Checks in hosts (the author's; the synth makes sound when a take plays):**

1. **Names**: click a take's name in the inspector: a "Rename take" box
   on the host's window; Backspace and typing edit it, and nothing in the
   host happens (in REAPER, with "Send all keyboard input to plugin" off).
   Undo and redo; save, close and reopen: the name is back, and the next
   take recorded counts on. And whether the grid's keys (Delete, arrows)
   now reach Umpire in REAPER after a click in its window.
2. **Scales**: set a take's EDO in the inspector ("Set by hand"); reopen
   the project: it's kept. A take recorded from KeyboardEDO in 31-EDO shows
   31 rows without setting it.
3. **Export**: Export take... writes `Take 1.midi2`; it opens in the
   standalone app's grid (umpire-daw, or another app that reads MIDI Clip
   Files) with the same notes. On Linux, export over it: "Replace it?".
4. **Import**: put the playhead on bar 5, Import clip... that file: a new
   take at bar 5, chosen, heard; undo takes it away.
5. **The synth menu**: choose another installed instrument (a moment's
   wait the first time); it plays at once, while the host plays too. Undo:
   PlainOldSynth comes back with its patch (change the patch first, in its
   window). Two tracks with different synths; save and reopen: each keeps
   its own.
6. **A missing synth**: with `plugin.conf`'s synth installed, open a
   project whose synth isn't (or rename its `.clap` for a moment): "... not
   installed" in amber, silence; save, put the `.clap` back, reopen: the
   synth and its patch are back.
7. **The input menu**: choose a source; the status shows it; unplug it
   ("(gone)") and plug it back. Choose one, save, and reopen where it isn't
   there: "(not here)". Default, None.
8. **The file panels open in front**, as a sheet on the plugin's window
   (REAPER on the Mac: the first try opened behind it, before the sheet);
   in Bitwig on the Mac too, whose plugin host is a separate process.

**Not in P4:**
- **Dragging a clip file** onto the lane, and **exporting several takes**
  or the whole lane: one take at a time, through the menu.
- **Clip names in the clip files** (Flex Data's Clip Name), as in the
  standalone app (files.md): the name is in the state, and an exported
  file's name is the take's.
- **Rescanning for synths** installed while the host runs: the menu's list
  is made once per process.
- **Other formats**: the menu lists CLAP instruments only (no VST3 or AU
  synths, which uw-core can't host yet).

**What it replaces:** milestones 13 to 18, T1, S1, S2 and the plugin-routing
proposal (R1 to R6). Milestone 12 becomes a question for P2. Zoom (Z1)
carries over to the plugin's lane and grid.

## Open questions

- **The name.** naming.md's rules (no "MIDI"; Umpire) suggest "Umpire" for
  the plugin as well; the standalone app might then become a test bed, not
  a product.
- ~~**The recording UI**: whether the host's record button is enough, or the
  plugin needs an arm of its own~~: it needs one (decided 2026-11-19, P2,
  "The arm").
- **Hardware output** from the plugin (above): needed or not.
- **Which open-source host to recommend**, once the checks are in (above).
