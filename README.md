# Umpire

A DAW built on MIDI 2.0's Universal MIDI Packet (UMP) from the ground up,
rather than MIDI 1.0 with MIDI 2.0 added on. It starts MIDI-only and Linux
first, and may take on audio later. An [unlikelyware.com](https://unlikelyware.com)
product.

Status: planning. The only code so far is milestone 1's UI spike
(`spike/`, findings in [docs/ui-spike.md](docs/ui-spike.md)), which builds
against a StageHand checkout's DGL:

```bash
git submodule update --init
cmake -S . -B build -G Ninja && ninja -C build
build/umpire-ui-spike
```

- [Decisions](docs/decisions.md): what's decided, and the current milestone
- [Roadmap](docs/roadmap.md): the milestones to a first useful version
- [UI spike](docs/ui-spike.md): milestone 1's findings on DGL plus Dear ImGui
- [Naming](docs/naming.md): product name, repo, binary, IDs
- [Ideas](docs/ideas.md): candidate UI and data ideas, design principles, open
  questions (nothing decided)
- [Plugin roadmap](docs/plugins-roadmap.md): MIDI 2.0 features to add to our
  CLAP synths

MIT licensed; see [LICENSE](LICENSE).
