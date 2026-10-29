# Umpire

A DAW built on MIDI 2.0's Universal MIDI Packet (UMP) from the ground up.
MIDI-only to start, Linux first. Milestones 4 to 6 are built: `umpire-daw`
is the track view, which records; see [docs/recording.md](docs/recording.md).

## Read first

1. [docs/decisions.md](docs/decisions.md): what has been decided and the
   working method; [MILESTONES.md](MILESTONES.md): the milestones.
2. [docs/naming.md](docs/naming.md): product name, binary, IDs, namespace.
3. To build: [BUILDING.md](BUILDING.md) (Linux and macOS).
4. As needed: [docs/ideas.md](docs/ideas.md) (candidate ideas, MIDI 2.0
   features, design principles), [docs/plugins-roadmap.md](docs/plugins-roadmap.md),
   [docs/ui-spike.md](docs/ui-spike.md) (milestone 1: DGL plus Dear ImGui),
   [docs/skeleton.md](docs/skeleton.md) (milestone 4: how `umpire-daw` is built),
   [docs/track-view.md](docs/track-view.md) (milestone 5: tracks and MIDI in),
   [docs/recording.md](docs/recording.md) (milestone 6: transport, clips,
   recording, undo; pitch and the grid's tuning),
   [docs/ui-direction.md](docs/ui-direction.md) (the UI direction: only what
   you need; read before any UI work).

Umpire shares code with StageHand (`~/Dev-MIDI/StageHand` on the Mac,
`~/dev/dev-midi/StageHand` on the Linux PC), a live-performance CLAP host.
Its docs (`docs/architecture.md`, `docs/sequences.md`, `docs/rack-view.md`,
`docs/macos.md`) explain the code Umpire will reuse.

## On the Mac

The Mac hasn't run the shared code yet. Before Umpire work there:

1. Clone uw-core (private; the same GitHub access as StageHand's):
   ```bash
   git clone --recurse-submodules https://github.com/erickampman/uw-core.git ~/Dev-Common/uw-core
   ```
   If the author keeps it elsewhere, use that path.
2. Follow `~/Dev-Common/uw-core/docs/macos-check.md`: uw-core's tests,
   StageHand's `uw-core-switch` branch over it, and the app checks (the
   audible ones are the author's). Record the results there, and in this
   repo's `MILESTONES.md` ("Outside Umpire (related)").
3. In each Mac clone with submodules (this one, StageHand, uw-core), once:
   `git config push.recurseSubmodules check`.

## Working with the author

- The author commits to `main` directly. Never open pull requests. Ask before
  pushing.
- Submodules (uw-core, the DPF fork, DPF-Widgets): push the submodule's commit
  before the pin that points at it. Use `git push --recurse-submodules=check`,
  or set `git config push.recurseSubmodules check` once per clone, on the
  Linux PC and on the Mac (decisions.md, "DPF fork").
- One milestone per session, about a day of work each.
- Keep tests silent (no audible output). The author tests anything audible
  themselves; warn before doing something that makes sound.
- Record decisions in the repo's docs, not only in conversation: sessions on
  other machines can't see earlier conversations or local memory.
