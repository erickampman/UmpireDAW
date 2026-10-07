# UI spike: DGL plus Dear ImGui (milestone 1)

Milestone 1 (decisions.md) checked the UI decision: one DGL window on Linux
with a NanoVG canvas and a Dear ImGui panel, working together. Done
2026-10-07.

**Result: it works.** DPF-Widgets' ImGui widget builds unpatched against
StageHand's DGL, both layers draw in one window without interfering, and
input can be routed correctly between them. The decision stands, and Qt isn't
needed. Using the stock widget as it ships causes three problems: input goes
to the wrong layer, it redraws at 60 fps forever, and its font rescale is
broken. The spike fixes all three by subclassing the widget. The author's
hand test passed (2026-10-07). Separately from ImGui, a DGL window stalled
once with vsync on under XWayland. The cause isn't pinned down, and StageHand
doesn't show it (finding 6).

## What was built

`umpire-ui-spike` (`spike/`), built by the top-level `CMakeLists.txt`:

- **`NoteCanvas`**: the window's `NanoTopLevelWidget`, as in StageHand's
  `UiWidget`. It draws a note grid with a keyboard, black-key bands and bar
  lines. A click adds a note, a drag moves it, a right-click deletes it, Delete
  removes the selected note, Up and Down transpose it, and the wheel scrolls.
- **`ImGuiLayer`**: DPF-Widgets' `ImGuiSubWidget`, the canvas's only
  subwidget, covering the whole window. It draws a menu bar (File, Edit, View)
  and an inspector panel at the right with the clip-name text field, the
  selected note's pitch, velocity, start and length, and frame and routing
  counters.
- **`SpikeModel.h`**: the model both layers show. It contains no DGL or ImGui
  types.
- **`SelfTest`**: `--self-test DIR` sends scripted events through the
  top-level widget's handlers, which is the path the window uses, and checks
  where each one went and how many frames were drawn. It writes screenshots
  (PPM, read back from the GL buffer) to `DIR`. Don't touch the window while it
  runs: real pointer motion or exposes add frames and events. One run in 19
  failed that way.

### Building and running

```bash
git submodule update --init
cmake -S . -B build -G Ninja
ninja -C build
build/umpire-ui-spike                   # try it by hand
build/umpire-ui-spike --log-frames      # print frames drawn per second
build/umpire-ui-spike --self-test /tmp/shots
DPF_SCALE_FACTOR=2 build/umpire-ui-spike --self-test /tmp/shots   # HiDPI
```

CMake finds DGL in a StageHand checkout (`../StageHand`,
`~/Dev-MIDI/StageHand` or `~/dev/dev-audio/StageHand`, each with
`/external/dpf`); otherwise pass `-DUMPIRE_DPF_DIR=<StageHand>/external/dpf`.
This is temporary until the shared library's location is decided. DGL gets
its own namespace, `UmpireDGL`, as StageHand has `StageHandDGL`.

DPF-Widgets is a git submodule (`external/DPF-Widgets`, pinned to `730da63`).
Only `opengl/DearImGui.cpp` is compiled; it `#include`s ImGui's sources itself.

## Findings

### 1. Building against StageHand's DGL

StageHand's DGL is upstream DPF plus three local patches (PMU's scale-factor
patch in `WindowPrivateData.cpp`, StageHand's `puglSetResizable()`, and macOS
fixes). DPF-Widgets targets upstream DPF, and its ImGui widget compiled
against StageHand's copy unchanged, with GCC 15, C++20 and the legacy-OpenGL
`dgl-opengl` that StageHand uses. It uses DGL's shared resources for its font
(DejaVu Sans, as StageHand does).

### 2. Versions, maintenance, docking

- **DPF-Widgets** is maintained by falkTX (DPF's author). Its last commit was
  2026-02-02. The ImGui widget last changed 2025-09 (a GL3 fix). It's used by
  Ildaeil, dear-plugins, master_me and others.
- **ImGui 1.91.9b**, from the master branch, so **no docking**. (DPF-Widgets'
  README still says 1.91.1b.) DPF's changes to ImGui are a 65-line patch
  (`opengl/DearImGui/dpf-changes.diff`, to the OpenGL backends and the demo),
  so building the docking branch in its place looks straightforward. Docking
  within the window would work. ImGui windows outside the main window
  (multi-viewport) would not, because DGL gives ImGui no platform windows. Not
  tried.
- ImGui 1.92 rewrote fonts and textures (dynamic font sizes, a new backend
  texture API). Moving DPF-Widgets past 1.91 will take work there or here.
  1.91.9b is fine for now.

### 3. Drawing together

The canvas is the window's top-level widget and the ImGui layer is its
subwidget, so DGL draws NanoVG first and ImGui over it, in one frame. No GL
state conflicts were seen: NanoVG leaves no shader program bound, and
DPF-Widgets' GL2 renderer saves and restores its own state. Menus and the
demo window draw over the canvas correctly (screenshots at 1×, 1.5× and 2×).

Because the canvas draws before ImGui in each frame, a change made in the
panel (dragging velocity, say) reaches the canvas one frame later. Scheduling
a few frames after input (finding 5) covers it.

### 4. Input routing

DGL doesn't hit-test subwidgets. Every visible subwidget is offered every
mouse event, topmost first, until one returns true. So what the ImGui widget
returns decides everything. The stock widget gets this wrong in three ways:

- **Presses** return `io.WantCaptureMouse`, which ImGui updates only once per
  frame. A move followed by a click in the same event batch can go to the
  wrong layer.
- **Motion** always returns false, so the canvas also sees motion over the
  panel and during ImGui drags: a slider dragged out over the grid moves the
  canvas's hover highlight.
- **Keys** return `io.WantCaptureKeyboard`. With
  `ImGuiConfigFlags_NavEnableKeyboard`, which the widget sets, that is true
  whenever an ImGui window has focus. After any click on the panel, the
  canvas would lose Delete, the arrows and Ctrl-Q.

These come from reading the code; the self-test checks the spike's rules,
not the stock widget's. The spike's rules (`ImGuiLayer::onMouse` and the
handlers after it):

- A **press** goes to ImGui if an ImGui popup is open, or if an ImGui window
  is under the pointer. That is tested at the press position with
  `ImGui::FindHoveredWindowEx()` against the last frame's windows. Everything
  else goes to the canvas.
- ImGui still **hears** every press, so a click on the canvas ends text
  editing. A press on the canvas with a menu open only closes the menu, as
  menus do.
- **Motion and the release go to whichever side took the press**, until
  release. Hovering over ImGui hides the canvas's hover highlight.
- The **wheel** goes to whatever is under the pointer.
- **Keys** go to ImGui only while a text field is active or a popup is open.
  Keyboard navigation is turned off. ImGui still hears every key, so it never
  misses a release.

The self-test checks each of these, along with a menu item that lies over the
canvas, a slider dragged out over the canvas, and a canvas drag released over
the panel. All pass.

The widget's state (`PrivateData`, which holds the ImGui context) is opaque
outside `DearImGui.cpp`. A subclass gets the context by calling
`ImGui::GetCurrentContext()` right after the base constructor, which leaves
it current.

### 5. Redrawing

**The stock widget redraws at 60 fps forever.** It adds a 16 ms idle callback
that calls `repaint()` unconditionally, so the whole window is drawn and
swapped every frame (measured: 63 frames per second while idle). That goes
against StageHand's rule of drawing only when something visible changes.

The spike draws on demand instead:
- Input reaching ImGui (and changes made by the canvas) schedules 3 frames.
- The idle callback repaints only while frames are owed.
- It also keeps drawing while ImGui's input queue isn't empty. ImGui trickles
  queued key events over several frames, so without this, fast typing lost
  characters: " two" came out " tw".
- The text caret doesn't blink (`io.ConfigInputTextCursorBlink = false`),
  which would otherwise need a frame every half second while a field is
  active.

Measured: 0 frames while idle, 1 frame for a pointer move over the canvas, a
few per click or keystroke. The View menu switches back to the stock behaviour
for comparison.

Each frame draws both layers, because a DGL repaint redraws the whole window.

### 6. A vsync stall, seen once (not ImGui's)

The first self-test runs failed almost every check because only about 2
frames a second were drawn. One `app.idle()` took 997 ms, which was
`glXSwapBuffers` waiting. That fits XWayland throttling a window the
compositor isn't presenting to about 1 Hz while vsync is on: each swap then
blocks the thread that swaps. DGL never sets pugl's `PUGL_SWAP_INTERVAL`
hint, so the driver's default (vsync on) applies.

With vsync off (Mesa's `vblank_mode=0`, or `glXSwapIntervalEXT(…, 0)`, which
the spike now calls after creating the window), everything passed and the
longest `app.idle()` was about 28 ms. The compositor still composites, so
there's no tearing; on-demand drawing keeps the frame rate low anyway.
`--vsync` keeps the driver's default.

**What triggers it isn't pinned down.** Being covered isn't enough. The
author ran StageHand with a sequence editor window open and playing, which
redraws it continuously, and put another window in front of it: no stall
(2026-10-07). So the self-test window was in some other state that the
compositor doesn't present. It may not have been shown yet, or it may have
been on another workspace or minimized. Not investigated further.

Turning vsync off is still cheap insurance against it, since the thread that
swaps also runs plugin editors. It's a small patch to the shared DGL (set
`PUGL_SWAP_INTERVAL` to 0 when the view is made), but no longer urgent.

Seen on Ubuntu (GNOME, Wayland) with XWayland and Intel UHD 630 (Mesa).

### 7. HiDPI and the scale factor

- **At startup** DGL reads the scale factor from `DPF_SCALE_FACTOR` or
  `Xft.dpi`. The stock widget scales ImGui's style and font by it once.
  Window and event coordinates are physical pixels for both layers. The
  canvas scales its NanoVG drawing as StageHand's widgets do; ImGui works in
  pixels directly. At 1.5× and 2× everything scaled together, text was sharp,
  and every routing check passed.
- **A change at runtime** (moving to another monitor): PMU's DGL patch calls
  `Window::onScaleFactorChanged()`. The stock widget has no hook for it.
  DPF-Widgets' `setFontSize()` would be the way to rebuild the font, but **it
  is broken**: it rebuilds ImGui's font atlas without recreating the GL
  texture made from it, so every glyph comes out as garbage (screenshot
  `05-rescale-stock`). The spike's `rescale()` resets and rescales the style,
  rebuilds the font, then recreates the texture
  (`ImGui_ImplOpenGL2_DestroyFontsTexture()` and `CreateFontsTexture()`), in
  the next frame, where the GL context is current. Checked by rescaling in the
  self-test; **not tried on a real move between monitors**.
- GNOME auto-maximized the window when the size asked for was near the
  screen's (1867×1131 at both 1.5× and 2×). Resizing still worked.

### 8. macOS

Nothing found that would block the port:
- The ImGui widget uses the same legacy OpenGL as StageHand's NanoVG, which
  StageHand's macOS port already uses (StageHand's macos.md, decision 8).
- Cmd: DPF-Widgets passes DGL's `kModifierSuper` to ImGui as Super, and
  ImGui's `io.ConfigMacOSXBehaviors` (on by default when built for Apple)
  makes Cmd its shortcut key. That matches StageHand's decision 7.
- The `UmpireDGL` namespace keeps pugl's Objective-C class names apart from
  hosted DPF plugins', as `StageHandDGL` does.

To check on the Mac:
- whether an occluded window's swap blocks there too (finding 6);
- the scale factor when moving between Retina and non-Retina displays (PMU's
  macOS patches);
- that ImGui's menu bar is drawn inside the window, so the macOS menu bar
  stays empty unless Umpire adds a native one (StageHand has none either:
  macos.md, decision 1).

## Checked by hand

The self-test sends events at the top-level widget, skipping pugl and X11.
The author checked the following by hand, and **all passed** (2026-10-07):

- typing in the clip-name field: shifted characters, key repeat, Ctrl-A, C,
  V and X, Home and End, and a compose key or input method (DGL's
  character-input events);
- opening each menu with the mouse, choosing items, Escape to close;
- dragging notes, including releasing over the panel, and dragging the
  inspector's sliders out over the grid;
- resizing the window from its edges, and maximizing it;
- View > "Redraw every frame" against the default, with `--log-frames`
  (expect no lines while idle, about 60 a second with it on);
- View > "Dear ImGui demo window": a large ImGui surface over the canvas;
- `DPF_SCALE_FACTOR=1.5` and `2`, and a real second monitor if available.

## Proposals (not decided)

1. **Keep DGL plus Dear ImGui** (the milestone's question): it works.
2. **Own the ImGui widget.** The spike replaces DPF-Widgets' redraw, routing
   and rescale, and reaches past its opaque `PrivateData`. Umpire would be
   better served by its own widget, a few hundred lines, adapted from DPF-Widgets'
   ISC-licensed one. It would still use the ImGui sources and DPF's patch to
   them, and could switch to the docking branch.
3. **Turn vsync off in DGL** (`PUGL_SWAP_INTERVAL` 0), for Umpire and
   StageHand, once the shared DGL has a home. Insurance, not urgent
   (finding 6).
4. Where the shared library lives was decided after the spike: its own repo
   (decisions.md, "Shared code"). Until it exists, Umpire builds against a
   StageHand checkout's DGL.
