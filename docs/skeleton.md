# Umpire's skeleton (milestone 4)

The first `umpire-daw`: Umpire's own CMake project over uw-core and the DPF
fork, one main window, and the view-model rule kept by the build. Built and
checked on the Mac on 2026-10-23, and on the Linux PC the same day (GCC
15.2, CMake 4.2, alsa-lib 1.2.15, X11): no warnings, `ctest` and
`--self-test` at 1, 1.5 and 2 all pass.

## Building and running

[BUILDING.md](../BUILDING.md) has the dependencies and steps for both
platforms; the commands as of milestone 4:

```bash
git submodule update --init --recursive
cmake -S . -B build -G Ninja && ninja -C build
ctest --test-dir build                     # silent tests
build/umpire-daw                           # the window
build/umpire-daw --log-frames              # ... printing frames drawn per second
build/umpire-daw --self-test /tmp/shots    # scripted input, checks, screenshots (PPM)
DPF_SCALE_FACTOR=2 build/umpire-daw --self-test /tmp/shots   # HiDPI (Linux)
```

Submodules: `external/uw-core` (uw-core, `a7a80ee`), `external/dpf` (the DPF
fork, `10c859cf`, which pins the pugl fork), `external/DPF-Widgets` (for
Dear ImGui's sources only). `UMPIRE_DPF_DIR` is gone. On the Mac,
`push.recurseSubmodules` was already `check` in this clone.

## What's in it

| Target | Sources | Sees the toolkit |
|---|---|---|
| `umpire-app` | `src/App`: `ViewModel`, `MainLoop` | no |
| `umpire-ui` | `src/Ui`: `MainWindow`, `GridCanvas`, `Panels`, `ImGuiWidget`, `Widgets`, `SelfTest` | DGL, NanoVG, ImGui |
| `umpire-imgui` | ImGui 1.91.9b from DPF-Widgets, legacy-OpenGL backend | (third-party) |
| `umpire-daw` | `src/App/main.cpp` | no: only `Ui/MainWindow.h` |
| `test-view-model` | `tests/ViewModelTests.cpp` | no |

- **The view-model rule is kept by CMake:** DGL's and ImGui's include paths
  are on `umpire-ui` only, so a toolkit header in `src/App` doesn't compile.
  `MainWindow.h` is a pimpl (as StageHand's `RackUi.h`), so `main.cpp` never
  sees DGL.
- **The main loop** is StageHand's `MainLoop` (poll() on Linux, AppKit's loop
  on macOS), copied into `src/App`. A 10 ms timer runs the window's
  `idle()` (DGL's events and drawing), as StageHand's rack window runs from
  one. SIGINT and SIGTERM quit. It may move into uw-core with the plugin
  editor windows (milestone 11), since it serves them in StageHand.
- **The view model** (`ViewModel`) is still the spike's note grid: a clip of
  notes in sixteenths, with the edits both layers make (add, select, move,
  transpose, velocity, length, delete) and a version count. Milestone 6's
  clip model replaces the notes.
- **The window** is the spike's, rebuilt: the NanoVG note grid is the
  top-level widget (`GridCanvas`), and the ImGui panels (`Panels`: the menu
  bar and the inspector) are its one subwidget, covering it. It won't stay
  the main window: that's the track view (decisions.md, "The track view";
  milestone 5), and the grid becomes the clip editor a clip opens in. Done
  in milestone 5 (track-view.md): this window is now the test grid window.
- **Umpire's own ImGui widget** (`ImGuiWidget`, ui-spike.md proposal 2) is
  adapted from DPF-Widgets' ISC-licensed one, with the spike's fixes built
  in rather than overridden: the routing rules (finding 4), drawing on
  demand (finding 5), and a rescale that rebuilds the font's GL texture
  (finding 7). It no longer reaches into DPF-Widgets' opaque `PrivateData`.
  Only DPF-Widgets' ImGui sources and DPF's patch to them are used now;
  moving to the docking branch would be a change to `umpire-imgui` alone.
- **Vsync is off** through the fork's `DGL_SWAP_INTERVAL=0` (finding 6). The
  spike's own GLX call (`Vsync.cpp`) is gone.
- **The house look:** `src/Ui/Widgets.{h,cpp}` are KeyboardEDO's
  (`kedo::app::ui`, at KeyboardEDO `42f0948`), copied into `umpire::ui` with
  only the namespace and the colours changed. The inspector uses them: group
  boxes for Clip, Note and Diagnostics; Velocity as a glowing knob with its
  caption and value box; Pitch, Start and Length as slider rows. The ImGui
  font is DejaVu Sans from DGL's shared resources, as is the canvas's.

## Decided here, for the author to confirm

- **Umpire's colours are provisional.** Graphite panels (26, 28, 33),
  sections a step lighter (36, 39, 46), wells darker (17, 18, 22), slate
  borders (104, 110, 124); an amber accent (240, 170, 50) for what's on,
  active or selected (the spike's selection colour); a teal (72, 182, 196)
  for notes and their controls. KeyboardEDO's colours were the author's
  call; so are these. They're in `Widgets.h` (`colour::`) and, for NanoVG,
  in `GridCanvas.cpp`.
- **The plugins' NanoVG widgets aren't copied yet.** The canvas has no knob
  or group box to draw yet; they come when it does (decisions.md,
  "Milestone 4" still holds: copied, not shared).
- **What the spike had that's gone:** View > "Redraw every frame" (it
  compared DPF-Widgets' stock widget, which Umpire no longer uses) and the
  self-test's check of it, and the deliberately broken "stock" rescale
  screenshot. View > Diagnostics shows the frame and routing counters.
- **Cmd+Q on the Mac, Ctrl+Q on Linux** quits from the canvas.
- On Linux the X11 class (WM_CLASS) is `Umpire`, as StageHand's is
  `StageHand`.

## Checked

On the Mac (macOS 26, Apple silicon, scale factor 2), 2026-10-23:
- builds with no warnings; `ctest` passes (the view model's edits);
- `--self-test` passes every check: no frames while idle, routing of
  presses, keys, text entry, menus over the canvas, drags out of either
  layer (the velocity knob turned down over the grid), the wheel, a resize,
  and a font rescale. The screenshots looked right.
- The first three self-test runs drew 20 to 40 frames a second while idle,
  before any scripted input, and failed that one check. Instrumented, the
  frames weren't from the test's input or from ImGui's; every run since has
  drawn none. Probably the window being exposed from outside (the desktop,
  or the pointer over it); not pinned down. Watch for it on Linux.
- `umpire-daw --log-frames` under the main loop: 3 frames at startup, none
  after; SIGINT quits cleanly.

Still to do, by the author:
- By hand, on both: ui-spike.md's "Checked by hand" list against
  `umpire-daw` (typing with shifted characters, key repeat and Ctrl/Cmd-A,
  C, V, X; menus with the mouse and Escape; drags; resizing; a second
  monitor), and the knob and sliders by feel.
- Say whether the colours stay.
