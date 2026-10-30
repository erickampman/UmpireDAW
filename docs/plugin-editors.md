# Plugin editor windows (milestone 11)

A track whose output is a CLAP instrument (milestone 8, docs/plugins.md)
can open that plugin's own editor, in its own window, from the row: an
"E" button beside the output choice, next to the R/M/S badges. Built and
self-tested on the Mac (Apple Silicon, macOS 26, Cocoa) on 2026-10-30; done
out of order, before files (9) and the piano roll (10), at the author's
request. Not yet built on Linux, and no editor has been opened on a real
plugin yet: both are the author's.

## Building and running

As milestone 8 ([BUILDING.md](../BUILDING.md)):

```bash
ctest --test-dir build                     # silent: unchanged, no editor window opens in a test
build/umpire-daw                           # the track view
build/umpire-daw --self-test /tmp/shots    # scripted (silent; never opens an editor)
```

To open one: give a track a CLAP instrument as its output (docs/plugins.md),
then click the "E" button beside the output choice. Click it again, or the
window's own close button, to close it; the track keeps playing either way.

## What's in it

| Piece | Where | Toolkit |
|---|---|---|
| `EditorWindow`: a top-level window hosting one plugin's editor, X11 on Linux, Cocoa on macOS | uw-core `src/uw/plugin/EditorWindow.{h,cpp,mm}` (moved from StageHand) | no |
| `uw::editor-window` CMake target | uw-core `CMakeLists.txt` | no |
| `AudioEngine::VoiceHost`: forwards CLAP's timer-support and posix-fd-support to `MainLoop` | `src/App/AudioEngine.cpp` | no |
| `AudioEngine::openEditor`/`closeEditor`/`editorOpen` | `src/App/AudioEngine.*` | no |
| `Player::openEditor`/`closeEditor`/`editorOpen`: dispatches to the track's stream, if it's a plugin's | `src/App/Player.*` | no |
| `TrackController::openEditor`/`closeEditor`; `TrackRowModel::outputIsPlugin`/`editorOpen` | `src/App/TrackController.*`, `src/App/TrackViewModel.h` | no |
| The "E" button beside the output choice | `src/Ui/TrackPanels.cpp` | ImGui |

### Moved from StageHand into uw-core

`EditorWindow` (StageHand's `App/EditorWindow.{h,cpp}` for X11,
`App/EditorWindowMac.{h,mm}` for Cocoa) already had the same class name and
public interface on both platforms, picked by `#ifdef __APPLE__` the way
`uw-core`'s own `MainLoop.h` already splits one header across two
implementation files -- so moving it was mostly decoupling it from
`stagehand::MainLoop`, not redesigning it:
- **A `uw::HostServices&` instead of a concrete `MainLoop&`.** The only
  loop services either platform's `EditorWindow` ever used were
  `watchFd`/`unwatchFd` (Linux's X11 connection; Cocoa's own run loop needs
  neither) and `requestQuit()` -- and `HostServices` already declares the
  first two with the exact right shape (it's the same interface a plugin's
  own CLAP timer-support/posix-fd-support requests go through). `requestQuit`
  isn't part of `HostServices`, so it's now a separate
  `std::function<void()>` parameter to `EditorWindow::open()`, alongside
  `save` (StageHand's `Ctrl/Cmd-Shift-S`).
- **What StageHand still owns**: StageHand hasn't switched to this yet (its
  own `App/EditorWindow*` is unchanged); that's separate work in StageHand's
  own repo (as "StageHand switches to uw-core" and "StageHand switches to
  the DPF fork" were, MILESTONES.md "Outside Umpire").

### Wiring a voice's HostServices to Umpire's MainLoop

Milestone 8's `AudioEngine::VoiceHost` answered a plugin's
`addTimer`/`watchFd` requests with `HostServices`' defaults ("not
available"), noted there as deferred until editors existed. It now takes
`AudioEngine`'s own `MainLoop&` and forwards every loop method to it, so a
plugin editor's redraw timer (CLAP's `timer-support`, the way a plugin with
no event loop of its own asks the host to drive it) and any fd it watches
actually run, serviced by the same 10 ms main-loop tick that already drives
the track view. `AudioEngine`'s constructor takes a `MainLoop&` now
(`main.cpp` makes it before `AudioEngine`, not just before `run()`).

### Opening and closing

- **One editor per voice**, owned by its `Voice` (a `std::unique_ptr<
  uw::EditorWindow>`): `AudioEngine::openEditor(voice, title, requestQuit)`
  calls `uw::EditorWindow::open(plugin, voiceHost, title, requestQuit)`,
  false if the plugin has none this platform can embed.
  `closeEditor`/`editorOpen` are direct forwards. `Player::openEditor` finds
  the track's stream and, only if it's a plugin's, calls through; a MIDI
  track's output has no editor at all.
- **Closing a voice closes its editor first** (`close()`, `v.editor.reset()`
  before `deactivate()`), matching `HostedPlugin`'s own lifecycle rule ("the
  editor closes before its plugin is destroyed").
- **The window can close itself** (its own close button, or the plugin
  tearing its editor down): `idle()` (called every tick) notices
  `!editor->isOpen()` and drops it, so a later `openEditor()` makes a fresh
  one instead of reusing a closed `EditorWindow`, and `editorOpen()`
  reports it gone within one tick.
- **No "quit" or "save" from an editor window yet**: `requestQuit` is left
  unset from `TrackController::openEditor`, so Ctrl/Cmd-Q inside a plugin's
  editor does nothing special (StageHand's reason for it -- quitting safely
  saves a titled rack -- doesn't apply yet: Umpire has nothing to save
  until milestone 9). Revisit once there is.

### Tests still never open a real window

`tests/PlaybackTests.cpp`'s fake plugin doesn't override `hasEditor()`
(`HostedPlugin`'s default, `false`), so `openEditor()` on it always fails
cleanly through the exact same path a plugin with no editor takes in the
real app -- `uw::EditorWindow::open()` never gets far enough to touch X11
or Cocoa. This is as far as a plugin editor can be tested headlessly;
opening one on a real CLAP instrument's editor is the author's, as hearing
one play is for milestone 8.

## Checked

On the Mac (Apple Silicon, macOS 26.2, Cocoa), 2026-10-30:
- No warnings, `-Wall -Wextra -Wpedantic`, a from-scratch build, including
  the new `uw::editor-window` target (its own Objective-C++ file, ARC).
- `test-playback`: all the milestone 6, 7 and 8 checks still pass, plus
  opening an editor on the fake plugin (no editor) fails cleanly and leaves
  nothing open, and a MIDI track's output has no editor to open at all.
- The other suites pass unchanged.
- `--self-test`: unaffected (no CLAP instrument is scanned there at all,
  milestone 8's deferred scan, so no row ever shows an Edit button in it).

## Still to do, by the author

- **Linux**: build and `ctest` there (the X11 `EditorWindow.cpp`, not
  exercised on this Mac at all); a real editor embedded in an X11 window.
- **Open a real CLAP instrument's editor** (StruckSynth, ModicumFM, or
  another plugin module) from a track's row, resize it, close it, reopen
  it, with the track still playing throughout.
- Whether the "E" button reads right beside the output combo.
- **StageHand's own switch** to `uw::editor-window` (dropping its copy of
  `App/EditorWindow*`): separate work, in StageHand's own repo.

## Not in it (later)

- **Ctrl/Cmd-Q and Ctrl/Cmd-Shift-S from an editor window**: `requestQuit`
  and `save` exist in the seam (`EditorWindow::open`'s parameters) but
  nothing in Umpire supplies them yet; worth revisiting once milestone 9
  gives the app something to save.
- **A clip's own instrument overriding the track's** (ui-direction.md): a
  clip reference's own editor isn't a thing yet; only a track's output is.
- **More than one editor window matters at once** in any special way
  (tiling, remembering positions): each is a plain window the window
  manager or the user places; Umpire doesn't manage their layout.
