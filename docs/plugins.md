# Hosting CLAP instruments (milestone 8)

A track's output can be a CLAP instrument instead of a MIDI device: its own
hosted instance, fed the track's clips exactly as a MIDI device is (the same
scheduling, the same lookahead), mixed into the platform's audio output
(JACK on Linux, CoreAudio on macOS). Built and self-tested on the Mac
(Apple Silicon, macOS 26, CoreAudio) on 2026-10-30; built on the Linux PC,
with Umpire's `ctest` and uw-core's own tests passing there, and heard by
the author with PlainOldSynth as a track's output, on 2026-11-01.

Chosen for the Mac rather than JACK, which the Mac build doesn't have at
all (BUILDING.md): uw-core's audio seam already carries both backends
behind one interface (`uw::AudioBackend`), the same way `uw::MidiOutput`
already picks ALSA or CoreMIDI, so nothing above `App/PlatformAudioBackend.h`
knows or cares which one is running underneath.

## Building and running

As milestone 7 ([BUILDING.md](../BUILDING.md)):

```bash
ctest --test-dir build                     # silent: now with test-audio-engine too
build/umpire-daw                           # the track view; opens the audio backend
build/umpire-daw --self-test /tmp/shots    # scripted (silent; never opens real audio)
```

To hear it: touch a track, choose its output under its name — a CLAP
instrument now sorts in after the MIDI devices — and play. Only a CLAP
instrument scanned from the usual locations (`~/.clap`, `/Library/Audio/
Plug-Ins/CLAP` or `$CLAP_PATH`) is offered; note effects and audio effects
aren't rack rows yet.

## What's in it

| Piece | Where | Toolkit |
|---|---|---|
| `AudioBackend`, `JackBackend`, `CoreAudioBackend`; the CLAP host (`ClapPlugin`, `ClapPluginFactory`) | uw-core `src/uw/audio/`, `src/uw/clap/` | no |
| `createAudioBackend`: JACK on Linux, CoreAudio on macOS, picked at compile time | `src/App/PlatformAudioBackend.h` | no |
| `Instruments`: scans for CLAP instruments, encodes/decodes a track's output id | `src/App/Instruments.*` | no |
| `AudioEngine`: voices, the per-voice queue, the real-time callback, `renderBlock()` | `src/App/AudioEngine.*` | no |
| `Player`: dispatches a stream to `MidiOutputs` or `AudioEngine`, by its device id | `src/App/Player.*` | no |
| The output choice, now listing instruments too | `src/Ui/TrackPanels.cpp` (unchanged: it already drew `TrackViewModel::outputs`) | ImGui |

### A plugin is another kind of output

`Session::Output.device` already held an opaque id with milestone 7
(`"client name:port name"` on ALSA); a plugin output is the same field,
holding a different kind of opaque id (`Instruments::outputId`, the
plugin's path and its id within it, joined by a separator no path or id
contains). `Instruments::isPluginOutput` tells the two apart, so everywhere
that already worked by a track's output id — the row, undo, the session,
saving later — didn't have to change. `Player` is the one place that reads
it: `openStream`/`closeStream`/`streamSchedules`/`streamProtocol`/
`streamSendAt`/`streamCancel` dispatch to `MidiOutputs` or `AudioEngine`,
and everything else in `Player.cpp` (`follow`, `join`, `silence`,
`schedule`, `chase`, `thru`...) is unchanged from milestone 7: a plugin
voice is scheduled, chased and muted exactly as a MIDI device is.

### AudioEngine: a voice per track, sample-accurate

- **One instance per track** (a "voice"), so one track's plugin never hears
  another's notes and muting one never touches another's audio. `open()`
  instantiates (`uw::PluginFactory::create`) and activates it;
  `AudioEngine::kMaxBlockFrames` (8192) is PipeWire's documented largest
  graph buffer (StageHand's docs/architecture.md, "Engine consequence"), so
  activation never has to guess the block size in advance; splitting a
  bigger block is a safety net the engine doesn't need yet and doesn't
  build (Not in it, below).
- **`Player` schedules a voice exactly as it schedules a MIDI device**:
  `AudioEngine::sendAt(voice, messages, count, when)` is the same shape as
  `MidiOutputs::sendAt`, and `schedules()` is always true — a voice holds
  what it's sent to its time, as a scheduling MIDI device does. What's held
  is a lock-free queue of its own (`uw::SpscQueue`, the main thread the only
  writer, the audio thread the only reader): no lock, no allocation, no
  syscall crosses the thread boundary.
- **Sample-accurate timing.** Each block, `renderBlock()` drains every event
  due by the block's end and turns its nanosecond time into its exact
  sample offset from the block's start
  (`AudioCallbackContext::blockStartTime`, the same convention uw-core's
  audio backends already keep for the engine's own clock). A note
  scheduled 3 ms into a 10 ms block lands on sample 144 at 48 kHz, not
  "sometime in this block" — checked directly in
  `tests/AudioEngineTests.cpp` and through the whole `Player`/
  `TrackController` path in `tests/PlaybackTests.cpp`.
- **Cancelling** (a stop, a locate, muting) can't remove an already-queued
  event from a lock-free FIFO the audio thread alone drains, so it doesn't
  try to: `cancel()` bumps a generation counter, and every queued event
  carries the generation it was sent under; `renderBlock()` discards one
  whose generation is stale instead of delivering it. The effect is the
  same as ALSA's `cancelScheduled` (a cancelled note never sounds), reached
  a different way because the queue is one-directional.
- **Removing a voice follows `HostedPlugin`'s own contract**: the audio
  thread must call `stopProcessing()` before the main thread may
  `deactivate()`. `close()` asks for that (an atomic flag) and waits for the
  audio thread to retire it — bounded, since `renderBlock()` services every
  voice every block — or, if the engine was never started, does it directly
  (nothing else ever calls `renderBlock()`, so there's nothing to wait
  for).
- **Mixing**: every voice's main audio output is summed into the backend's
  output buffer. No gain, pan, mute or master bus yet — that's the mixer
  (decisions.md, "Audio: printing, mixdown, playback"; MILESTONES.md 14).

### Offline-capable

`renderBlock()` is the entire audio-thread path and never reaches for the
backend; `process()` (`uw::AudioProcessor`'s real-time callback) is a
one-line forward to it. `AudioEngine` works exactly the same way with no
backend at all (`backend == nullptr`) or before `start()` is called: a
voice still opens, activates (at a default 48 kHz until a real rate is
known) and processes, and `tests/AudioEngineTests.cpp` and the plugin test
in `tests/PlaybackTests.cpp` drive it entirely this way, by hand, block by
block. This is also what decisions.md asked milestone 8 to leave in place
for the audio phase after v1 ("the engine can run offline, without the
audio device, to render"): a future offline render drives these same
voices through `renderBlock()` with its own clock, never opening a device,
and milestone 8 didn't have to add anything to make that true — it just
had to not couple `renderBlock()` to the backend in the first place.

### Linux: pw-jack

Umpire links the ordinary `libjack`, jackd2's, which reaches only a real
`jackd`. On a PipeWire system there's none: PipeWire's JACK layer is
reached by running under `pw-jack`. Before 2026-10-30, started without it,
`engine.start()` failed silently, so a track's plugin output opened and
was fed notes but nothing was ever heard (found on the Linux PC, the first
time a plugin was played there). Now, as StageHand does (its
docs/architecture.md and docs/command-line.md, "Audio: pw-jack"),
`main.cpp` checks `uw::JackBackend::serverAvailable()` before anything
opens and, with no server, restarts itself under `pw-jack` ("No JACK
server in reach; restarting under pw-jack."). At most once
(`UMPIRE_PW_JACK=1` marks the restarted process); a real `jackd`, if one
runs, is used as is; `--self-test` never restarts (it never opens audio).
If the backend still won't open, Umpire says "No audio output: CLAP
instruments will be silent." Not chosen: switching the system's `libjack`
to PipeWire's (`ld.so.conf.d`), which would rule out a real `jackd` for
every program (StageHand's reasoning). The outputs, `Umpire:out_1` and
`out_2`, connect to the system's playback ports by themselves.

### macOS: CoreAudio, not JACK

The Mac has no JACK at all (BUILDING.md, "macOS"): `CoreAudioBackend` opens
the system's default output device. `App/PlatformAudioBackend.h` picks it
at compile time, as `uw/midi/PlatformMidiOutput.h` already picks CoreMIDI
over ALSA. `AudioEngine` and `Player` never see which backend is running;
only `main.cpp`'s one `createAudioBackend("Umpire")` call does.

### Tests, and --self-test, never scan or load a real plugin

`Instruments` starts empty: rescan() does the scanning, and it loads every
`.clap` it finds (`ClapPluginFactory::scan()` has to, to read a plugin's
descriptors), so its owner calls it only where that's safe. `main.cpp` calls
it right beside `engine.start()`, both after the self-test's early return,
so `umpire-daw --self-test` opens real windows but never scans or loads a
real plugin, and never opens real audio. `tests/AudioEngineTests.cpp` and
the plugin test in `tests/PlaybackTests.cpp` give `AudioEngine` a fake
`uw::PluginFactory` and no backend (`nullptr`), call `rescan()` themselves
(cheap and safe: the fake never touches a file), and drive `renderBlock()`
by hand.

This isn't caution for its own sake: scanning every `.clap` under
`~/.clap` and `$CLAP_PATH` on this Mac, before this was fixed, loaded two
of the author's own in-progress plugins (IFSynth and ModicumFM)
into `umpire-daw`'s own process just to list them, and they
turned out to define the same Objective-C class name
(`WebViewDelegate__DISTRHO`) -- a real collision ("spurious casting
failures and mysterious crashes", objc's own words), caught by
`--self-test` disturbing its idle-frame checks before a human ever saw it.
Worth carrying into the plugin roadmap: every DPF-based plugin needs a
build-time-unique identity the way `DGL_NAMESPACE` already gives each app
one (decisions.md, "DPF fork"), not only once two of them happen to load
into the same host at once.

## Checked

On the Mac (Apple Silicon, macOS 26.2, CoreAudio), 2026-10-30:
- No warnings, `-Wall -Wextra -Wpedantic`, a from-scratch build.
- `test-audio-engine` (new): two instruments found and sorted by name,
  opening an unknown id fails, two voices open as separate instances, a
  voice's label and protocol, sample-accurate delivery (a note 3 ms into a
  10 ms block lands on sample 144, nowhere else), two voices mixed (summed)
  rather than overwriting each other, cancel drops what's pending without
  disturbing what's sent after it even due in the same block, closing one
  voice doesn't disturb another, and the engine is offline throughout
  (`running()` false) since nothing here ever calls `start()`.
- `test-playback`: all the milestone 6 and 7 checks still pass, plus a
  track given a fake CLAP instrument as its output: its row shows the
  plugin's name, a recorded clip plays into it, and the note arrives at
  AudioEngine's worked-out sample offset, driven through `renderBlock()` by
  hand (no backend ever opened).
- The other two suites pass unchanged.
- `--self-test`: never scans or loads a plugin, or opens real audio (both
  are after the self-test's early return in `main.cpp`), so the output
  combo shows only MIDI devices there. The track view's own checks pass
  (occasional flakiness in the idle-frame checks remains, as milestone 6
  and 7 found with the KeyLab 88 connected -- not a regression here; it
  went away with the scan deferred, below). The clip grid window's own
  checks show the same keyboard/mouse-focus flakiness noted in
  playback.md, unrelated to this milestone.

## Still to do, by the author

- [x] **Linux**: build and `ctest` there; `uw-core`'s own tests on that
  machine (2026-11-01, all passing).
- [x] **Listen**: a real CLAP instrument as a track's output (PlainOldSynth,
  2026-11-01: works).
- [x] Whether the output combo reads right with instruments mixed in after the
  MIDI devices (2026-11-01: it does).

## Not in it (later)

- **The mixer** (gain, pan, mute, solo per track beyond what already mutes
  a MIDI track; a master bus): milestone 14.
- **Plugin editors**: milestone 11. A plugin's parameters only ever sit at
  their defaults for now; `HostServices::requestRestart` /
  `requestParamFlush` are accepted but not acted on (nothing yet asks a
  voice to change its port layout or flush params while stopped).
- **Transport info to plugins** (tempo, bar, playing): `ProcessContext::time`
  only carries the sample rate; a tempo-synced effect would hear the wrong
  thing. Umpire doesn't send MIDI clock either yet (playback.md).
- **Native CLAP note events and per-note expression**: a voice is fed MIDI
  1.0 or 2.0 (whichever the plugin's note port prefers); a plugin whose
  note port accepts only `CLAP_NOTE_DIALECT_CLAP` gets nothing yet
  (StageHand's docs/architecture.md, "MIDI 2.0 per-note data → CLAP note
  expressions... deferred").
- **Hot sample-rate changes** while running: a voice is reactivated when
  the engine starts at its real rate, but not if the backend's rate changes
  again afterward (rare; JACK/PipeWire and CoreAudio don't do this during
  an ordinary session).
- **Detecting a new or removed plugin** without restarting: `Instruments`
  scans once, when `main.cpp` asks it to.
- **Scanning out of process**, or any other isolation from a plugin that
  crashes or collides while merely being scanned (the ObjC collision
  above): every plugin host has this problem; not solved here.
- Block splitting for a backend that ever asks for more than
  `kMaxBlockFrames` (8192) in one callback: a safety net kept in mind, not
  built.
