# Contributing to Umpire

Building and testing: [BUILDING.md](BUILDING.md).

## Before each commit

```bash
ctest --test-dir build --output-on-failure
```

Keep tests silent (no audible output); the author tests anything audible by
hand, so warn before doing something that makes sound. Record a design
decision in [docs/decisions.md](docs/decisions.md), not only in a commit
message or conversation: sessions on other machines can't see either.

## Changing uw-core or the DPF fork

When Umpire's change needs a uw-core change (or a DPF/pugl one), commit and
push uw-core (or pugl, then DPF) first, then commit Umpire's new pin.
Otherwise the other machine can't fetch the pin. To have git refuse such a
push:

```bash
git push --recurse-submodules=check             # this push only
git config push.recurseSubmodules check         # the default for this clone
```

The `git config` line is per clone: run it in every clone that has
submodules (this one, StageHand, uw-core), on each machine (decisions.md,
"DPF fork").

## Code layout rule

`src/App` has no DGL or ImGui types: the view models and the track
controller are plain C++, tested headless with a fake MIDI input. Only
`src/Ui` uses the toolkit (the view-model rule, kept by CMake; see
decisions.md and skeleton.md).

## Checking UI changes without looking

`umpire-daw --self-test DIR` runs the window's own checks, scripted through
its event handlers, and saves screenshots (binary PPM) to DIR. It opens two
windows and is silent; don't touch the windows or play MIDI while it runs.

## The docs

Start with [docs/decisions.md](docs/decisions.md) (what's decided, and the
working method) and [MILESTONES.md](MILESTONES.md). Since 2026-11-18 the
product is the plugin (`src/Plugin`, `Umpire.clap` and `Umpire.vst3`):
Umpire as an instrument inside REAPER, Bitwig or Ardour
([docs/plugin-version.md](docs/plugin-version.md), milestones P2 to P5).
The `umpire-daw` app goes on beside it (decisions.md, "The app goes on").
Then, as needed:

- [docs/naming.md](docs/naming.md): product name, binary, IDs, namespace.
- [docs/ui-direction.md](docs/ui-direction.md): the UI direction (only what
  you need); read before any UI work.
- [docs/ideas.md](docs/ideas.md): candidate ideas, MIDI 2.0 features, design
  principles; [docs/plugins-roadmap.md](docs/plugins-roadmap.md).
- The app's milestones: [docs/ui-spike.md](docs/ui-spike.md) (1: DGL plus
  Dear ImGui), [docs/skeleton.md](docs/skeleton.md) (4: how `umpire-daw` is
  built), [docs/track-view.md](docs/track-view.md) (5: tracks and MIDI in),
  [docs/recording.md](docs/recording.md) (6: transport, clips, recording,
  undo; pitch and the grid's tuning), [docs/playback.md](docs/playback.md)
  (7: outputs, scheduling, the loop, chasing Pitch 7.25),
  [docs/plugins.md](docs/plugins.md) (8: hosting CLAP instruments, the audio
  engine, sample-accurate timing), [docs/files.md](docs/files.md) (9: the
  `.umpire` session bundle, MIDI Clip Files, the File menu),
  [docs/clip-editor.md](docs/clip-editor.md) (10: the grid edits its clip),
  [docs/plugin-editors.md](docs/plugin-editors.md) (11: plugin editor
  windows, in uw-core).
- Replaced by the plugin, not built: [docs/plugin-routing.md](docs/plugin-routing.md)
  (the routing graph and mixers), [docs/stem-export.md](docs/stem-export.md)
  (stems and the tempo map).

Umpire shares code with [StageHand](https://github.com/erickampman/StageHand),
a live-performance CLAP host, through uw-core. StageHand's
`docs/architecture.md`, `docs/sequences.md`, `docs/rack-view.md` and
`docs/macos.md` explain the code Umpire reuses.

## Working method

One milestone per session, about a day of work each, with a fresh session
per milestone. Start by reading [docs/decisions.md](docs/decisions.md),
[MILESTONES.md](MILESTONES.md) and the docs they name.
