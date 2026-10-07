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
  library that both use. Where that library lives (its own repo, a
  submodule, or inside one of the repos) is not decided yet.
- **Plugins:** Umpire hosts CLAP plugins from the first version.

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
- The roadmap beyond milestone 1 hasn't been written yet. Draft it after
  milestone 1 settles the UI question.

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

Next: draft the roadmap (see Working method), deciding on ui-spike.md's
proposals: owning the ImGui widget, and turning vsync off in DGL.

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
