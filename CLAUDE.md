# Umpire

A DAW built on MIDI 2.0's Universal MIDI Packet (UMP) from the ground up.
MIDI-only to start, Linux first. Planning stage.

## Read first

1. [docs/decisions.md](docs/decisions.md): what has been decided and the
   working method; [docs/roadmap.md](docs/roadmap.md): the milestones.
2. [docs/naming.md](docs/naming.md): product name, binary, IDs, namespace.
3. As needed: [docs/ideas.md](docs/ideas.md) (candidate ideas, MIDI 2.0
   features, design principles), [docs/plugins-roadmap.md](docs/plugins-roadmap.md),
   [docs/ui-spike.md](docs/ui-spike.md) (milestone 1: DGL plus Dear ImGui).

Umpire shares code with StageHand (`~/Dev-MIDI/StageHand` on the Mac,
`~/dev/dev-audio/StageHand` on the Linux PC), a live-performance CLAP host.
Its docs (`docs/architecture.md`, `docs/sequences.md`, `docs/rack-view.md`,
`docs/macos.md`) explain the code Umpire will reuse.

## Working with the author

- The author commits to `main` directly. Never open pull requests. Ask before
  pushing.
- One milestone per session, about a day of work each.
- Keep tests silent (no audible output). The author tests anything audible
  themselves; warn before doing something that makes sound.
- Record decisions in the repo's docs, not only in conversation: sessions on
  other machines can't see earlier conversations or local memory.
