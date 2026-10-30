# Decisions

Decisions made while planning Umpire (2026-10-07), so that sessions on any
machine can see them. Naming is in [naming.md](naming.md), candidate ideas in
[ideas.md](ideas.md), and the plugin work in
[plugins-roadmap.md](plugins-roadmap.md).

## Scope

- **Audience:** the author plus early adopters. No packaging or installers
  until later.
- **First useful version:** record MIDI 2.0 from KeyEDO or hardware, edit it
  in a piano roll that preserves per-note data, play it back to plugins or
  hardware, and save it. One linear timeline.
- **Relationship to StageHand:** code both apps need (UMP types, the CLAP
  host, the MIDI and audio backends) moves out of StageHand into a shared
  library that both use (see Shared code, below).
- **Plugins:** Umpire hosts CLAP plugins from the first version.

## Shared code (decided 2026-10-07)

- **Its own repo: `uw-core`** (decided 2026-10-07), in
  `~/dev/dev-common/uw-core` on the Linux PC and private on GitHub
  (`erickampman/uw-core`) for now.
  Neither StageHand nor Umpire holds the other's code. One repo, with the
  layers as separate CMake targets (`uw::ump`, `uw::clap-host`, `uw::audio`,
  ...) and the C++ namespace `uw`. The company name keeps it neutral: it
  isn't only MIDI (the CLAP host, the audio backends) and isn't either app's.
- **Static libraries**, built by each app's CMake. No shared library (.so):
  the apps aren't packaged, and static linking avoids ABI and install-path
  questions.
- **A git submodule in each app**, as StageHand has CLAP. Each app pins its
  own commit, so a change made for one app reaches the other only when its
  pin is moved.
- **Contents:** UMP types, the CLAP host, and the MIDI and audio backends
  behind their interfaces. DPF is not in it (below).
- **Layered for plugins (confirmed 2026-10-07):** a **UMP core** target
  (`uw::ump`) with no dependencies (message types,
  decoding by message type and status, Pitch 7.9, later per-note data),
  largely header-only. The synth and effect plugins (StruckSynth, StruckKit,
  IFSynth, ModicumFM, ...) can use it as well as the apps, so everything
  decodes UMP the same way. Built with `-fPIC` (it's linked into `.clap`
  shared objects) and kept hidden inside each plugin (`-fvisibility=hidden`),
  so two plugins pinned to different versions don't bind to each other's
  symbols. The CLAP host and backends are separate targets that only the apps
  build.
- **Building uw-core is separate from using it.** Making the repo and moving
  the code into it (from StageHand, with uw-core's own tests) is its own
  milestone, the roadmap's first. Switching each consumer to the submodule
  (StageHand, Umpire, later each plugin) is separate work in that consumer's
  repo; StageHand's tests must still pass after its switch.
- **Licence: MIT** (decided 2026-10-07), as Umpire's.

## DPF fork (confirmed 2026-10-07)

Today every DPF user has its own modified copy. StageHand's carries PMU's
patches and its own. Each plugin has a `dpf` submodule with separate
changes, including the CLAP wrapper's UMP handling.

- **DPF becomes its own private repo** on GitHub (in `~/dev/dev-common`
  beside uw-core on the Linux PC): a fork of upstream DPF
  holding all the local patches (PMU's, StageHand's, the plugins').
  StageHand, Umpire and every plugin use it as a git submodule, each pinned
  to a commit. It replaces StageHand's vendored copy and Umpire's
  `UMPIRE_DPF_DIR`.
- **pugl is forked too (decided 2026-10-14):** upstream DPF holds pugl as a
  submodule, and PMU's macOS patches are in pugl. So pugl has its own private
  fork (`erickampman/pugl`, `~/dev/dev-common/pugl`), which the DPF fork
  (`erickampman/DPF`, `~/dev/dev-common/DPF`) pins. Upstream merges stay
  plain git merges in both; the cost is one more repo to push first (pugl,
  then DPF, then the consumer), and CI tokens need access to both.
- **Both machines:** a submodule is a URL and a commit, so the Linux PC
  and the Mac work the same way, whatever the local paths. Each needs access
  to the private repo (the same SSH key or credentials as StageHand's). Clone
  with `--recurse-submodules`, and run `git submodule update --init` after a
  pull. The macOS patches (PMU's Retina fixes, StageHand's `pugl.mm`, the file
  browser) then reach every consumer.
- **Workflow:** change DPF on a branch inside a consumer's submodule, push the
  fork, then commit and push the consumer's new pin. The same applies to
  uw-core.
- **Push the submodule first.** Use `git push --recurse-submodules=check`, or
  set `git config push.recurseSubmodules check` once per clone, so git
  refuses to push a pin the submodule's remote doesn't have yet. The setting
  is per clone: set it in every clone with submodules (StageHand, Umpire,
  each plugin), on the Linux PC and on the Mac.
- **Every consumer sets its own `DGL_NAMESPACE`** (`StageHandDGL`,
  `UmpireDGL`, `StruckSynthDGL`, ...). On macOS pugl names its Objective-C
  classes after it, and two binaries in one process with the same name
  collide (StageHand's macos.md, milestone 5). With a shared fork, two
  plugins would otherwise register the same classes.
- **Moving the plugins onto the fork** is separate work in the plugin repos,
  and belongs to the plugin roadmap rather than Umpire's: diff each plugin's
  `dpf` against upstream, merge the changes into the fork, and repoint the
  submodule.

## Platform and technology

- **Development** happens on the Linux PC. Linux comes first; the macOS port
  comes later, following StageHand's (see `StageHand/docs/macos.md`).
- **Language and build:** C++ and CMake, as in StageHand.
- **UI: DGL plus Dear ImGui.**
  - DGL/pugl for windows. StageHand's X11 plugin-editor code and its macOS
    port carry over.
  - NanoVG for canvases (piano roll, arrangement), reusing StageHand's
    sequence editor, including its display of Pitch 7.9 notes on an EDO grid.
  - Dear ImGui for what DGL lacks: text entry, menus, inspectors, track
    lists, settings, dialogs. Integrated through DISTRHO's DPF-Widgets, which
    has an ImGui widget for DGL (check that it's maintained and works with
    StageHand's DGL version; that's milestone 1).
  - Keep StageHand's rule: the view model contains no DGL or ImGui types, so
    only the drawing layer depends on the toolkit.
  - Rejected: **Qt** (workable under LGPL, but StageHand's UI would be
    rewritten; kept as the fallback), **GTK 4** (removed X11 window
    embedding, so plugin editors can't be embedded, and plugins using other
    GTK versions clash in the same process), **JUCE** (AGPL or commercial),
    **wxWidgets** (uses GTK on Linux).
- **Backends:** ALSA sequencer UMP for MIDI (kernel 6.5+, alsa-lib 1.2.10+),
  JACK/PipeWire for audio, both behind interfaces so CoreMIDI and CoreAudio
  can be added for the Mac. JACK has no UMP support, which is why MIDI goes
  through ALSA.
- **Licence: MIT.** Probably open source. No Mac App Store. Dependencies are
  all compatible: CLAP (MIT), DPF/DGL (ISC, attribution required), Dear ImGui
  (MIT), nlohmann/json (MIT), alsa-lib and libjack (LGPL 2.1, linked
  dynamically). Keep an attribution notice for each.

## Product, first version

- **From ideas.md:** StageHand's UI for Pitch 7.9 display; idea 5 (UMP groups
  as the routing backbone); idea 9 (the arrangement as a list of clip
  references, with transforms on the reference).
- **Arrangement:** a linear timeline only. A track is a view over a range of
  UMP groups.
- **File format:** Umpire's own session file, referring to clips stored as
  standard MIDI Clip Files (M2-116-U).
- **MIDI 1.0 hardware** works from the start, converted to UMP on the way in.

## MPE (decided 2026-10-11)

- **The policy, across the projects:** support no MPE feature that MIDI 2.0
  duplicates. Synths (PlainOldSynth and the other plugin modules) accept MPE
  as a compatibility input; StageHand passes it through untouched; KeyboardEDO
  doesn't send it; Umpire converts it on the way in and never sends it. The
  aims: push toward MIDI 2.0, and keep support for a transitional scheme small.
- **Input: MPE becomes MIDI 2.0 per-note data, behind a per-input "MPE"
  toggle.** A toggle, not detection of the MPE Configuration Message, because
  not every controller sends that message. The layout is the MPE lower zone
  (master channel 1, member channels 2-16). With the toggle on, a take is
  stored as MIDI 2.0 like any other:
  - a member-channel note becomes a note on the track's channel;
  - member-channel pitch bend becomes per-note pitch bend, rescaled from the
    member bend range (±48 by default, or what the controller set with RPN 0)
    to Umpire's per-note range;
  - member-channel pressure becomes polyphonic pressure, and member-channel
    CC74 becomes registered per-note controller 74;
  - master-channel messages (pitch wheel, sustain, mod wheel) become ordinary
    channel messages on the track's channel;
  - RPN 0 on a member channel sets the bend range the conversion uses, and the
    MPE Configuration Message resets it to the default.
- **Output: never MPE.** For MIDI 1.0 devices and plugins, per-note data
  becomes channel messages (ideas.md, 23).
- **Open:** a key held on two member channels at once can't be two notes with
  the same number on one MIDI 2.0 channel. Leaning: StageHand's sequencer rule
  (its domain-model.md): move the second to the nearest free note number, with
  a Pitch 7.9 attribute carrying its real pitch.
- **When:** not scheduled. It touches milestone 5's input path and depends on
  per-note data in the clip model (Deferred, below).

## Audio: printing, mixdown, playback (decided 2026-10-14)

Umpire stays MIDI-first, but it needs audio for these (naming.md already
allowed for it, as with MOTU's Digital Performer):

- **Print a MIDI track to an audio file**, the track's instrument rendered.
- **Two views of a printed track, stacked**: a MIDI view and an audio view
  (the waveform) of the same track, in time with each other. Either can be on
  top and visible; the one underneath shows as a slight overlap (offset like
  stacked cards), so it's clear there's another view.
- **Mix to a file** (a mixdown of the tracks).
- **Play audio files in the app**: printed tracks and the mix.

Decided:
- **When: right after the first useful version.** v1 stays MIDI-only (Scope,
  above), and the audio work is the next phase (MILESTONES.md, milestones
  14-18). But v1 is designed for it:
  - the engine can run offline, without the audio device, to render;
  - the session file has room for audio file references.
- **Printing covers plugin tracks first.** A CLAP instrument track is
  rendered offline, faster than real time, inside the app. Printing a
  hardware or KeyEDO track comes later. It needs audio inputs in `uw::audio`
  (JACK capture, CoreAudio input; StageHand has outputs only), real-time
  recording, and latency compensation.
- **Printing happens only when the user asks** (a command on a track, or
  the mixdown), never automatically or in the background. Editing the MIDI
  would make a background render wasted work. A print may render offline,
  faster than real time, once asked for. When its MIDI or the plugin's state
  changes, the print is marked out of date and kept as it is, until the user
  prints again.
- **A mixer is part of it:** per-track gain, pan, mute and solo, and a
  master bus. The mixdown renders through it.

Open (decide in the milestones):
- Printed audio belongs to its track, as the track's audio view, not to a
  separate audio track. Still open: the switch for what plays, the audio or
  the instrument (as freezing does, freeing the plugin), and whether it
  follows the view on top or is set separately. Also, how the stacked views
  look and swap (a click on the edge that shows, a key).
- Knowing when a print is out of date (the MIDI or the plugin's state changed
  since): a hash of what made it, stored with it.
- The file format and library: WAV at the session's sample rate, as 32-bit
  float for prints and with a choice of formats for the mix. Library
  candidates: dr_wav (single header, public domain or MIT-0), or libsndfile
  (LGPL, linked dynamically, which the licence decision allows).
- Where files go: in the session's folder, beside its MIDI Clip Files.

## Milestone 4: the spike and the widgets (decided 2026-10-22)

- **The spike is deleted** once `umpire-daw` does what it did. Git history
  keeps it, and ui-spike.md keeps what it taught. It isn't kept as a test
  bench.
- **The house widgets are copied into Umpire**, not shared. Umpire starts
  from copies of KeyboardEDO's `src/app/Widgets.{h,cpp}` (ImGui) and the
  plugins' NanoVG widgets (`src/UI/Knob.hpp` and the rest), in Umpire's own
  namespace and colours. uw-core stays free of UI code. A shared UI library
  can come later if the copies drift apart in ways that hurt.

Done 2026-10-23 (skeleton.md), on the Mac. Also decided there:
- **StageHand's `MainLoop` is copied into Umpire** (`src/App`), as the
  widgets are. It may move into uw-core with the plugin editor windows
  (milestone 11), which it serves in StageHand.
- **The view-model rule is kept by the build:** only `umpire-ui` has DGL's
  and ImGui's headers on its include path.
- **Umpire's colours are provisional** (graphite, an amber accent, a teal
  for notes) until the author settles them.
- **The plugins' NanoVG widgets are copied when the canvas needs one**, not
  before.

## The track view (decided 2026-10-23)

The author, after seeing milestone 4's grid: the grid won't be the usual way
MIDI gets in; playing it in will. So:
- **The main window is a list of track rows**, as StageHand's rack rows. A
  row says where the track's MIDI comes from (input, channels, groups), is
  armed to record or plays back, and says what gets its MIDI (a MIDI device
  or a hosted CLAP plugin). Mute, solo and an activity light, as the rack's.
- **Each track has a lane** on a timeline beside its row, holding its clips.
- **Double-clicking a clip opens it in the grid** (the piano roll): the grid
  is the clip editor, not the main view.
- **The grid opens in its own window** (the author, 2026-10-23), as
  StageHand's sequence editor does: one window per clip, its own DGL window
  with the NanoVG grid and its own ImGui panels. The main window keeps the
  tracks.
- The milestones were reordered for it: the track view and MIDI in (5), then
  transport and recording (6), playback to MIDI outputs (7), CLAP
  instruments (8), files (9), the piano roll (10) (MILESTONES.md).

- **StageHand's rack-view pattern, not its code** (milestone 5, 2026-10-23;
  track-view.md): its rack code is bound to its engine and session, so
  Umpire has its own view model, controller and rows in the same shape, and
  copies only `MidiSourceRef`, the group and channel masks, the rule for
  which inputs to open, and the monitor's `describeUmp`.
- **The track headers are ImGui, the lanes NanoVG**, lined up row for row
  from one scroll position the canvas keeps.

The UI direction (below) changes the rows: input moves to the session, and a
row shows only what isn't the default.

## The UI direction (decided 2026-10-23)

The author, after reading [daw-dynamic-ui-spec.md](daw-dynamic-ui-spec.md)
and seeing mockups: "the opposite of Bitwig: you see only what you need to
see." The details and the mockups are in [ui-direction.md](ui-direction.md).
- **Only what you need.** The timeline is always there; everything else
  appears because there is content, or because the user touched or asked
  for it. Details appear in place, zoom replaces opening things, and a
  command palette summons commands and task surfaces.
- **Hide controls, never hide state.** Anything that changes what you hear
  shows at a glance, at least as a badge.
- **Nothing changes on a guess.** Things appear because content exists or
  the user asked, and always in the same place.
- **Both ways to edit notes:** zooming into a clip's lane edits it in place,
  and double-clicking still opens the grid in its own window (The track
  view, above).
- **The model is a graph; rows are its default view.** A track is a node
  with a lane; inputs and outputs are edges. A touched track shows its path.
  A full routing view comes later, in 2D first.
- **Input belongs to the session.** Umpire listens to all inputs; playing
  goes to the touched track, recording to the armed tracks. Filtering by
  channel or group is asked for, not shown on every row. This replaces
  milestone 5's per-track input choices.
- **An instrument is a plugin or a device**, treated as equals. A clip
  reference may name its own instrument, overriding the track's.
- **One undo for everything**, as commands on the session, starting in
  milestone 6.
- **The spec is kept as a reference.** Its routing view and XY controller are
  candidate ideas (ideas.md, "From the dynamic UI spec"); its AI layer is
  not planned.

## Time, clips and pitch (milestone 6, 2026-10-23)

Made while building milestone 6 on the Linux PC (recording.md); the last two
points after the author asked how StageHand's sequences handle tunings other
than 12-EDO.
- **Ticks: 40320 to a quarter note** (8!), in 64-bit integers. It divides by
  1 to 10, 12, 14, 15 and 16, so triplets, quintuplets and septuplets are
  exact, and it fits a MIDI Clip File's 16-bit ticks per quarter (DCTPQ).
  Ticks are canonical; seconds come from the tempo (ideas.md, "Timebase").
- **A take is stored as MIDI 2.0.** MIDI 1.0 input is upconverted as it's
  recorded (uw-core's `upconvert`, one per input), as the MPE decision
  assumed ("a take is stored as MIDI 2.0 like any other"). Everything else a
  channel sends is kept as it came, Pitch 7.25 with no note and Per-Note
  Management included (MILESTONES.md).
- **Clips never change once made.** References and undo share them; an edit
  (milestone 10) makes a new clip.
- **Undo is snapshots of the session** before each command. Since clips are
  shared, a snapshot copies tracks and clip references, not music. IDs and
  track numbers live outside the snapshots, so none is reused.
- **A new session is empty**, showing only "Play something to start"; the
  first note played makes the first track.
- **No tuning is assumed in the clip model or the lanes.** A note's pitch is
  absolute, in 7.9: its Pitch 7.9 attribute, else the Pitch 7.25 last sent
  for its key (until a Per-Note Management reset), else its note number. The
  lanes draw notes at that pitch.
- **The grid uses StageHand's tuning model** (its docs/sequences.md, "EDO
  pitch"): an EDO and the note step 0 sounds; one row per step; rows dark
  where the pitch rounds to a 12-TET black key. A clip's notes land on the
  nearest row.
- **The clip holds its scale** (the author, 2026-10-23, after a 31-EDO take
  from KeyboardEDO showed on 12-EDO rows): not the track or the session.
  Built the same day (recording.md, "Working out a clip's scale"): worked out
  as a take becomes a clip; the grid opens in it, and a change there is a
  "Set scale" command on the clip.
- **Umpire should work the scale out from the notes** (the author:
  "ideally"), since a take's pitches are exact (7.9) and an EDO is a lattice
  they all sit on. It must find the reference too, not only the EDO:
  KeyboardEDO anchors its EDO at any 12-EDO pitch (A by default), and in
  31-EDO from A middle C isn't on the lattice (C is 7.75 steps above A), so a
  grid with step 0 at 60 is a quarter step off. The notes give the anchor
  away: it's where the lattice meets a whole MIDI note. Every scale is an EDO
  for now; others are long-term. How: ideas.md, "Working out a clip's
  scale".
- **A controller announcing its tuning** (MIDI-CI, Property Exchange) is a
  strong idea but depends on the controller; working it out from the notes
  is the fallback that works with any of them (ideas.md, same section).

## Playback (milestone 7, 2026-10-29)

Made while building milestone 7 on the Linux PC ([playback.md](playback.md)).
- **Timing is the platform's.** Playback sends 100 ms ahead with a time on
  uw-core's clock, and the platform holds each message until then
  (`MidiOutput::sendAt`, an ALSA sequencer queue), so timing doesn't depend
  on the main loop's 10 ms tick or a slow frame. Where a platform can't hold
  them (CoreMIDI, for now) the player sends on the tick instead.
- **A track's output is an edge of the graph** (`Session::outputs`), not a
  field of the track, as ui-direction.md's graph asks; setting it is a
  command. One output per track for now.
- **One output handle per track**, so a track can be cancelled on its own (a
  mute while playing) without cutting the others' notes.
- **A stop, a locate and a loop wrap end the notes sounding**; a tempo or
  loop change keeps them and retimes what's ahead.
- **Pitch 7.25 is chased per key**, on play, a locate and a wrap: the value in
  force at the playhead, or a Per-Note Management reset where none is, for
  each key the track's clips tune, because the synths keep it as a tuning
  table. Nothing else is chased yet.
- **Playing goes through to the touched track's output** (ui-direction.md:
  playing goes to the touched track), unless it's muted; a note-off follows
  its note-on.
- **The loop is the session's**, changed by commands, set by a drag along the
  ruler; it doesn't loop while recording (loop recording is later).
- **Umpire's own inputs aren't offered as outputs**, and Umpire's output is
  its own ALSA client pair, "Umpire Out".

## Hosting CLAP instruments (milestone 8, 2026-10-30)

Made while building milestone 8 on the Mac ([plugins.md](plugins.md)).
- **CoreAudio on macOS, not JACK**: the Mac build has no JACK dependency at
  all (BUILDING.md), so `App/PlatformAudioBackend.h` picks
  `uw::CoreAudioBackend` there and `uw::JackBackend` on Linux, as
  `uw/midi/PlatformMidiOutput.h` already picks CoreMIDI or ALSA. Neither
  `AudioEngine` nor `Player` knows which backend is running.
- **A plugin output is the same opaque id milestone 7 already used**
  (`Session::Output.device`), not a new field: `Instruments::outputId`
  encodes the plugin's path and its id within it, and
  `Instruments::isPluginOutput` tells it apart from a MIDI device id.
  Everything that already worked by a track's output id (the row, undo,
  the session) didn't have to change; only `Player` dispatches on it.
- **A plugin voice is scheduled exactly like a MIDI device**: `AudioEngine`
  mirrors `MidiOutputs`' shape (`schedules`/`protocol`/`sendAt`/`cancel`),
  so `Player`'s scheduling, chasing and muting code (milestone 7) is
  unchanged; only four small dispatch functions decide which of
  `MidiOutputs` or `AudioEngine` a stream talks to.
- **Cancelling a lock-free, one-reader queue**: `uw::SpscQueue` has no way
  to remove an item the consumer hasn't reached yet, so `AudioEngine`
  doesn't try to: `cancel()` bumps a generation counter and every queued
  event carries the generation it was sent under, so the audio thread
  discards a stale one instead of delivering it. Same effect as ALSA's
  `cancelScheduled`, reached differently because the queue only goes one
  way.
- **Removing a voice follows `HostedPlugin`'s own contract** (its header
  comment): the audio thread must call `stopProcessing()` before the main
  thread may `deactivate()`. `close()` asks for that (an atomic flag) and
  waits for the audio thread to retire it -- bounded, since every block
  services every voice -- or, if the engine was never started, does it
  directly, since nothing else ever calls `renderBlock()` then.
- **Offline-capable, by not coupling `process()` to the backend in the
  first place**: `renderBlock()` is the whole audio-thread path;
  `uw::AudioProcessor::process()` (the real-time callback) is a one-line
  forward to it. This is exactly what the audio phase after v1 will need
  (below, "Audio: printing, mixdown, playback": "the engine can run
  offline, without the audio device, to render"), and milestone 8 didn't
  have to add anything for it to be true -- every test in
  `tests/AudioEngineTests.cpp` and the plugin test in
  `tests/PlaybackTests.cpp` already drive it this way, by hand, with no
  backend at all.
- **Scanning for plugins is a separate, explicit step** (`Instruments`
  starts empty; `rescan()` does the work), called only where it's safe --
  `main.cpp` calls it beside `AudioEngine::start()`, both after
  `--self-test`'s early return -- because it loads every `.clap` it finds
  to read its descriptors. Found the hard way, building this: scanning
  loaded two of the author's own in-progress plugins (IFSynth and
  ModicumFM, `~/Dev-Audio`) into `umpire-daw`'s process just to list them,
  and they turned out to define the same Objective-C class name
  (`WebViewDelegate__DISTRHO`), a real collision that disturbed
  `--self-test`'s idle-frame checks before a human saw it. Worth carrying
  into the plugin roadmap: every DPF-based plugin needs a build-time-unique
  identity, the way `DGL_NAMESPACE` already gives each *app* one ("DPF
  fork", above), not only once two plugins happen to load into the same
  host at once.

## Plugin editor windows (milestone 11, 2026-10-30)

Made while building milestone 11 on the Mac ([plugin-editors.md](plugin-editors.md)),
done out of order (before 9 and 10) at the author's request.
- **Moved from StageHand into uw-core, not rewritten**: `EditorWindow`
  already had the same class name and public interface on both platforms in
  StageHand (`App/EditorWindow.{h,cpp}` for X11, `App/EditorWindowMac.{h,mm}`
  for Cocoa), so the move was decoupling it from `stagehand::MainLoop`, not
  a redesign: a `uw::HostServices&` covers the only loop services either
  platform used (`watchFd`/`unwatchFd`, Linux only), and a
  `std::function<void()> requestQuit` parameter replaces
  `MainLoop::requestQuit()`.
- **StageHand's own copy is untouched.** Switching StageHand to
  `uw::editor-window` is separate work in StageHand's own repo, as its
  earlier switches to uw-core and the DPF fork were (MILESTONES.md,
  "Outside Umpire").
- **A plugin editor's timer and fd requests now go somewhere**: milestone
  8's `AudioEngine::VoiceHost` answered them with `HostServices`' defaults
  ("not available"), deferred until editors existed. It now forwards to
  `AudioEngine`'s own `MainLoop&`, so a plugin editor's redraw timer (CLAP's
  timer-support, since an editor has no event loop of its own) runs on the
  same 10 ms tick that already drives the track view.
- **No "quit" or "save" from an editor window yet**: StageHand's reason for
  grabbing Ctrl/Cmd-Q there (quitting safely still saves a titled rack)
  doesn't apply until milestone 9 gives Umpire something to save, so
  `TrackController` doesn't supply either callback yet.

## Deferred

- **Per-note pitch bend and per-note controllers:** consider early how such
  data would be displayed and edited, so the design leaves room for it, but
  don't implement it in the short term.
- Procedural clips, content-addressed storage, the text view, vertical
  display, MIDI-CI, and everything else in ideas.md not listed above.

## Related projects

- **Plugins** (`~/Dev-Audio` on the Mac): the author is working on plugin
  milestone 0 (see plugins-roadmap.md).
- **KeyEDO** is being ported to Linux by the author. No MIDI 2.0 features
  beyond what it already supports, in the near term (see
  `KeyEDO/docs/controller-roadmap.md`).

## Working method

- **Milestones are about a day each, with a fresh Claude session per
  milestone.** StageHand used too few sessions. Each milestone starts by
  reading this file, the milestones, and the docs it names.
- The milestones are listed in [MILESTONES.md](../MILESTONES.md), drafted
  2026-10-07 after milestone 1. It adopts ui-spike.md's proposals: Umpire
  owns its ImGui widget (milestone 4), and the DPF fork turns vsync off
  (milestone 3).

## Milestone 1: DGL plus Dear ImGui spike (Linux)

The first milestone, and the check on the UI decision.

**Status: done 2026-10-07.** It works, and the UI decision stands. The
findings and proposals are in [ui-spike.md](ui-spike.md), with a list of
checks for the author to make by hand. The spike was `spike/`
(`umpire-ui-spike`), deleted in milestone 4. Set up during the milestone:
- DPF-Widgets is a git submodule (`external/DPF-Widgets`, pinned).
- DGL comes from a StageHand checkout (`UMPIRE_DPF_DIR`) until the shared
  library has a home.
- DGL's namespace is `UmpireDGL`.

The author's hand test passed. StageHand showed no vsync stall (ui-spike.md,
finding 6).

Next: [MILESTONES.md](../MILESTONES.md).

**Goal:** one DGL window on Linux containing:
- a NanoVG canvas (a simple note grid, or a piece of StageHand's sequence
  editor),
- a DPF-Widgets ImGui panel with a text field and a menu,
- both working together: typing in the text field, opening the menu,
  clicking and dragging on the canvas, resizing the window.

**What to find out:**
- Does DPF-Widgets' ImGui widget build against StageHand's DGL version? Is it
  maintained? Which ImGui version does it use, and does it have the docking
  branch?
- Do keyboard focus and mouse input go to the right place between ImGui and
  NanoVG?
- Does it redraw only when needed, or every frame?
- HiDPI and the UI scale factor.
- Anything that would block the macOS port later.

**Done when:** the window works as above, and the findings are written into
this repo (a short `docs/ui-spike.md`). If ImGui in DGL turns out not to
work, record why, and reconsider Qt.
