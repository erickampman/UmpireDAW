# Building Umpire

How to build `umpire-daw` and run its tests on Linux and macOS. Linux comes
first (decisions.md, "Platform"); the Mac builds the same app over CoreMIDI.
There are no CI builds yet, so this page is the reference.

Umpire is C++20, built with CMake (3.21 or later) and Ninja. Its own code is
`src/` and `tests/`; everything else comes in as git submodules, shared with
StageHand and KeyboardEDO where noted:

| Submodule | What | Access |
|---|---|---|
| `external/uw-core` | the code shared with StageHand: UMP, the clock and queues, the plugin seam and CLAP host, the MIDI and audio backends (namespace `uw`); brings the CLAP SDK as its own submodule | private (`erickampman/uw-core`) |
| `external/dpf` | the DPF fork, for DGL (windows, NanoVG); pins the pugl fork as its own submodule | private (`erickampman/DPF`, `erickampman/pugl`) |
| `external/DPF-Widgets` | only for Dear ImGui's sources (1.91.9b) and DPF's patch to them | public (DISTRHO) |

Everything else is a system library. While uw-core, DPF and pugl are
private, cloning needs access to all three (the same GitHub credentials as
StageHand's).

## Get the source

```bash
git clone https://github.com/erickampman/UmpireDAW.git
cd UmpireDAW
tools/fetch_deps.sh
```

`tools/fetch_deps.sh` checks out the git submodules at their pinned commits.
Run it again after a `git pull` that moves, adds or re-points a submodule
(milestone 4 added `external/uw-core` and `external/dpf`, 2026-10-23): a
plain pull leaves them where they were.

**A build directory from before milestone 4** (the UI spike's, configured
with `UMPIRE_DPF_DIR`) is best removed rather than reused:

```bash
rm -rf build
```

Build from `main`.

## Linux

### Dependencies

| Package (Debian/Ubuntu) | Why |
|---|---|
| `g++`, `cmake`, `ninja-build`, `pkg-config` | toolchain |
| `libasound2-dev`, **alsa-lib 1.2.10 or newer** | MIDI input: the ALSA sequencer in UMP mode (uw-core) |
| `libjack-jackd2-dev` | JACK, for audio (uw-core builds it now; Umpire uses it from milestone 8) |
| `libx11-dev` | DGL's windows (pugl's X11 backend) |
| `libgl-dev` | OpenGL, for NanoVG and Dear ImGui |
| `libxcursor-dev`, `libxext-dev`, `libxrandr-dev` (optional) | pugl uses them when present: cursors, the X extensions, monitor geometry |
| `libdbus-1-dev` (optional) | DGL uses it when present |

```bash
sudo apt install g++ cmake ninja-build pkg-config libasound2-dev libjack-jackd2-dev libx11-dev libgl-dev libxcursor-dev libxext-dev libxrandr-dev
```

alsa-lib 1.2.10 means Ubuntu 24.04, Debian 13 or newer, and the kernel needs
ALSA's UMP support (6.5 or later). With an older alsa-lib, CMake stops at
uw-core's `pkg_check_modules(UW_ALSA ...)`. On other distros, install the
same libraries under their own names (`alsa-lib`, `jack2` or
`pipewire-jack`, `libX11`, `libXcursor`, `libXext`, `libXrandr`,
`mesa`/`libglvnd`, with their `-devel` packages where the distro splits
them).

### Build and test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The same three commands on both platforms. Everything goes in `build/`
(ignored by git). Without `-DCMAKE_BUILD_TYPE` the build type is
`RelWithDebInfo`; `-DCMAKE_BUILD_TYPE=Debug` gives a debug build.
`build/compile_commands.json` is there for clangd. The `ctest` suites are
silent and need no display, MIDI or audio: they drive the view models and
the track controller headless, with a fake MIDI input.

GCC 15 warns about `<cstdbool>` in pugl's headers; the build quiets that for
DGL only. Umpire's own code builds with `-Wall -Wextra -Wpedantic` and
should show no warnings.

### Run

```bash
./build/umpire-daw
```

It needs an X display (`DISPLAY` set; XWayland under Wayland). It opens the
ALSA sequencer as two clients, `Umpire` (MIDI 1.0) and `Umpire MIDI 2.0`,
opens every hardware input, and makes two virtual inputs, "MIDI 2.0 In" and
"MIDI 1.0 In", for KeyEDO and other apps to connect to (`aconnect -l` lists
them). If the sequencer can't be opened, the window still opens, without
MIDI.

The window's own checks, scripted through its event handlers, with
screenshots (binary PPM) in the directory given. They open two windows and
are silent; don't touch the windows or play MIDI while they run:

```bash
./build/umpire-daw --self-test /tmp/umpire-shots
```

At other scale factors:

```bash
DPF_SCALE_FACTOR=2 ./build/umpire-daw --self-test /tmp/umpire-shots
```

`--log-frames` prints the frames drawn each second (none while idle).

## macOS

Apple silicon, macOS 26 or later: the build sets `CMAKE_OSX_ARCHITECTURES`
to `arm64` and the deployment target to 26.0, as StageHand's. Checked with
Xcode 26.3 and CMake 4.1 (2026-10-23).

### Dependencies

Xcode or its Command Line Tools (AppleClang, C++20):

```bash
xcode-select --install
```

CMake and Ninja, for example from Homebrew:

```bash
brew install cmake ninja
```

No ALSA, JACK, X11 or OpenGL packages: the Mac uses system frameworks
(CoreMIDI, CoreAudio, CoreFoundation, Foundation, AppKit, OpenGL). OpenGL is
deprecated on macOS but still there; the build silences its warnings.

### Build and test

The same commands as on Linux:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### Run

```bash
./build/umpire-daw
```

It's a plain executable for now, not an app bundle (no `Umpire.app`, no
icon, no Dock entry of its own: the macOS port comes later, decisions.md).
Run it from a terminal; Cmd-Q, or closing the window, quits. It makes a
CoreMIDI client, `Umpire`, with the same two virtual inputs as on Linux,
which other apps (KeyboardEDO) see as destinations.

The self-test is the same:

```bash
./build/umpire-daw --self-test /tmp/umpire-shots
```

Retina displays work without a bundle: DGL reads the backing scale (2 on a
Retina screen), and the self-test prints it.

## Other platforms

Not supported. Umpire needs uw-core's MIDI backends, which exist for Linux
(ALSA) and macOS (CoreMIDI) only.

## How this differs from the other projects

Every Unlikelyware project builds the same way: `git clone`,
`tools/fetch_deps.sh`, the three commands under "Build and test". What's
different here, and why:

- No CI yet: StageHand and KeyboardEDO have a GitHub Actions workflow;
  Umpire doesn't.
- No `cmake --install` or packaged app yet: `umpire-daw` runs from `build/`
  on both platforms, with no app bundle on macOS (the macOS port is a later
  decision).
- Umpire also needs `external/DPF-Widgets`, for Dear ImGui's sources
  (1.91.9b): StageHand doesn't host plugin UIs this way, and KeyboardEDO
  vendors Dear ImGui as its own submodule instead.
