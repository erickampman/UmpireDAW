# Building Umpire

How to build `umpire-daw`, and the plugin spike (`Umpire.clap`, `Umpire.vst3`;
milestone P1, docs/plugin-version.md), and run the tests on Linux and macOS. Linux comes
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
| `external/clap-wrapper` | the plugin's VST3 and AUv2 builds: Unlikelyware's fork, branch `unlikelyware` (v0.16.0 and its AUv2 changes), pinned at `01f838b` (Retune's `a436757` plus the AUv2 transport and offline-render fixes) | public (`erickampman/clap-wrapper`) |
| `external/vst3sdk` | Steinberg's VST3 SDK for clap-wrapper, pinned as in Retune; only its `base`, `cmake`, `pluginterfaces` and `public.sdk` are fetched | public (Steinberg) |

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
| `zenity`, or `kdialog` on KDE (at run time, not to build) | File's Open and Save As dialogs (docs/files.md) |

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

The same build makes the plugin: `build/Umpire.clap`, and through
clap-wrapper `build/Umpire.vst3` (on Linux, in `build/Release/`, or
`build/Debug/`). `-DUMPIRE_BUILD_VST3=OFF` leaves the VST3 out,
`-DUMPIRE_BUILD_PLUGIN=OFF` both. Its tests (`test-plugin-*`) load the built
`.clap` and, if PlainOldSynth is installed, host it, rendering into buffers
only; without PlainOldSynth that one is skipped. To try it in a host, link
the bundles into the user's plugin folders (`~/Library/Audio/Plug-Ins/CLAP`
and `VST3` on the Mac, `~/.clap` and `~/.vst3` on Linux):

```bash
cmake --build build --target install-clap install-vst3
```

On the Mac the build also makes `build/Umpire.component`, an AUv2
instrument for Logic (`-DUMPIRE_BUILD_AUV2=OFF` leaves it out; on trial
since 2026-11-20, docs/plugin-version.md, "AUv2 for Logic"). clap-wrapper
fetches Apple's AudioUnitSDK at configure time unless `AUDIOUNIT_SDK_ROOT`
names a copy, such as Retune's:
`-DAUDIOUNIT_SDK_ROOT=../Retune/build/cpm/AudioUnitSDK`.
`install-auv2` copies it into `~/Library/Audio/Plug-Ins/Components`, with
the `Info.plist` that names it an Audio Unit (clap-wrapper puts that into
`build/Umpire.component` only when the binary links, and a CMake re-run
replaces it, so the build's copy may lack it); then validate it as Logic
will:

```bash
cmake --build build --target install-auv2
```

```bash
auval -v aumu Umpr nLKL
```

docs/plugin-version.md ("The spike, built") says how it's set up and where
it logs. Its window (P3) uses DGL and Dear ImGui, as the app's grid does.
On the Mac, `ninja -C build umpire-plugin-window-check` builds a small
host that opens the window and clicks in it (MILESTONES.md, "Tests"; it
opens a window, so it isn't a `ctest` suite).

GCC 15 warns about `<cstdbool>` in pugl's headers; the build quiets that for
DGL only. Umpire's own code builds with `-Wall -Wextra -Wpedantic` and
should show no warnings.

### Run

```bash
./build/umpire-daw
```

It needs an X display (`DISPLAY` set; XWayland under Wayland). Audio, for
CLAP instruments, goes through JACK, which on a PipeWire system is
PipeWire's JACK layer, reached under `pw-jack` (the `pipewire-jack`
package). You don't have to type it: started with no JACK server in reach,
`umpire-daw` restarts itself under `pw-jack`, once, as StageHand does
([docs/plugins.md](docs/plugins.md), "Linux: pw-jack"). If audio can't
open at all it says "No audio output" and plugins are silent. It opens the
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

### The dock icon

GNOME's dock (and the Ubuntu Dock) takes an app's icon from an installed
`.desktop` launcher whose `StartupWMClass` matches the window's WM_CLASS
(`Umpire`, set in `src/Ui/MainWindow.cpp`). Install one, for this user,
running this checkout's `build/umpire-daw`:

```bash
tools/install-desktop-entry.sh
```

It writes `~/.local/share/applications/umpire-daw.desktop` (from
`resources/linux/umpire-daw.desktop.in`) and the icons into
`~/.local/share/icons/hicolor`. Umpire then has its icon in the dock, however
it's started (the launcher, or `./build/umpire-daw` in a terminal), and
"Pin to Dash" keeps it there. Run it again after moving the checkout; log out
and in if the dock doesn't pick it up. Without the launcher, the windows
still carry the icon themselves (`_NET_WM_ICON`, `src/Ui/WindowIcon.cpp`),
which the window switcher shows.

The icons in `resources/linux/icons` are rendered from the macOS icon's
artwork (`resources/Umpire.icon`) by `tools/render-linux-icons.sh`
(ImageMagick); run it again when the artwork changes. StageHand does the
same with the same scripts.

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

`cmake --build build` makes `Umpire.app`, with its icon (compiled from
`resources/Umpire.icon` by Xcode's `actool`, as StageHand's
`resources/StageHand.icon`) in the Dock, Finder and Cmd-Tab. Without Xcode
(only the Command Line Tools), or with an Xcode older than 26, the build
warns and the bundle has no icon. `./build/umpire-daw` is a small script
that runs the bundle's real binary (`Umpire.app/Contents/MacOS/Umpire`), so
the commands above and the milestone docs still work unchanged. Run it from
a terminal; Cmd-Q, or closing the window, quits. It makes a
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
  on both platforms (on macOS as `Umpire.app`, for its icon; on Linux
  `tools/install-desktop-entry.sh` adds a launcher pointing at it).
- Umpire also needs `external/DPF-Widgets`, for Dear ImGui's sources
  (1.91.9b): StageHand doesn't host plugin UIs this way, and KeyboardEDO
  vendors Dear ImGui as its own submodule instead.
