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
  above), and the audio work is the next phase (roadmap.md, milestones
  13-17). But v1 is designed for it:
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
  reading this file, the roadmap, and the docs it names.
- The roadmap is [roadmap.md](roadmap.md), drafted 2026-10-07 after
  milestone 1. It adopts ui-spike.md's proposals: Umpire owns its ImGui
  widget (milestone 4), and the DPF fork turns vsync off (milestone 3).

## Milestone 1: DGL plus Dear ImGui spike (Linux)

The first milestone, and the check on the UI decision.

**Status: done 2026-10-07.** It works, and the UI decision stands. The
findings and proposals are in [ui-spike.md](ui-spike.md), with a list of
checks for the author to make by hand. The spike is `spike/`
(`umpire-ui-spike`). Set up during the milestone:
- DPF-Widgets is a git submodule (`external/DPF-Widgets`, pinned).
- DGL comes from a StageHand checkout (`UMPIRE_DPF_DIR`) until the shared
  library has a home.
- DGL's namespace is `UmpireDGL`.

The author's hand test passed. StageHand showed no vsync stall (ui-spike.md,
finding 6).

Next: [roadmap.md](roadmap.md).

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
