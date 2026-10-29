# Umpire

A DAW built on MIDI 2.0's Universal MIDI Packet (UMP) from the ground up,
rather than MIDI 1.0 with MIDI 2.0 added on. It starts MIDI-only and Linux
first, and may take on audio later. An [unlikelyware.com](https://unlikelyware.com)
product.

Status: early. `umpire-daw` is the track view (milestone 6): it listens to
every MIDI input, records the armed tracks into clips of UMP, with a
transport, undo and a MIDI monitor; a clip opens in a grid of EDO steps. See
[docs/recording.md](docs/recording.md) and [docs/skeleton.md](docs/skeleton.md).

```bash
git clone https://github.com/erickampman/UmpireDAW.git
cd UmpireDAW
tools/fetch_deps.sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/umpire-daw
```

[BUILDING.md](BUILDING.md) has the dependencies for Linux and macOS,
the tests, and running it.

- [Decisions](docs/decisions.md): what's decided, and the current milestone
- [Milestones](MILESTONES.md): the milestones to a first useful version
- [Building](BUILDING.md): Linux and macOS, dependencies, tests
- [Contributing](CONTRIBUTING.md): before each commit, submodule push order
- [Recording](docs/recording.md): milestone 6, transport, clips, undo, tuning
- [Track view](docs/track-view.md): milestone 5, the main window and MIDI in
- [Skeleton](docs/skeleton.md): milestone 4, what `umpire-daw` is built from
- [UI spike](docs/ui-spike.md): milestone 1's findings on DGL plus Dear ImGui
- [Naming](docs/naming.md): product name, repo, binary, IDs
- [Ideas](docs/ideas.md): candidate UI and data ideas, design principles, open
  questions (nothing decided)
- [Plugin roadmap](docs/plugins-roadmap.md): MIDI 2.0 features to add to our
  CLAP synths

MIT licensed; see [LICENSE](LICENSE).
