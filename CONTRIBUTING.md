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

## Working method

One milestone per session, about a day of work each, with a fresh session
per milestone. Start by reading [docs/decisions.md](docs/decisions.md),
[MILESTONES.md](MILESTONES.md) and the docs they name.
