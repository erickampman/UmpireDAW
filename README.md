# Umpire

A DAW built on MIDI 2.0's Universal MIDI Packet (UMP) from the ground up,
rather than MIDI 1.0 with MIDI 2.0 added on. It starts MIDI-only and Linux
first, and may take on audio later. An [unlikelyware.com](https://unlikelyware.com)
product.

Status: early. `umpire-daw` is milestone 4's skeleton: one window with a
NanoVG note grid under Dear ImGui panels, over uw-core and the DPF fork
(submodules). See [docs/skeleton.md](docs/skeleton.md).

```bash
git submodule update --init --recursive
cmake -S . -B build -G Ninja && ninja -C build
build/umpire-daw
```

- [Decisions](docs/decisions.md): what's decided, and the current milestone
- [Roadmap](docs/roadmap.md): the milestones to a first useful version
- [Skeleton](docs/skeleton.md): milestone 4, what `umpire-daw` is built from
- [UI spike](docs/ui-spike.md): milestone 1's findings on DGL plus Dear ImGui
- [Naming](docs/naming.md): product name, repo, binary, IDs
- [Ideas](docs/ideas.md): candidate UI and data ideas, design principles, open
  questions (nothing decided)
- [Plugin roadmap](docs/plugins-roadmap.md): MIDI 2.0 features to add to our
  CLAP synths

MIT licensed; see [LICENSE](LICENSE).
