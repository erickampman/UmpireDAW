# Files (milestone 9)

A session saves as a folder, `Name.umpire`, and opens again as it was: the
tracks, their clips (each a standard MIDI Clip File), the outputs, each
plugin output's state, the tempo, meter and loop. File has New, Open...,
Save and Save As..., and before New, Open or Quit drops unsaved changes it
asks first. Built and tested on the Linux PC on 2026-11-01; built and
tested on the Mac on 2026-11-01, with AppKit's own panels as its file
dialogs (checking them is the author's: ctest can't show a panel). The author
checks the menu, the prompt and the title by eye, and that a saved
session plays the same, plugins included, after reopening.

## Building and running

As before ([BUILDING.md](../BUILDING.md)). The file dialogs are the
desktop's own: on Linux zenity (`sudo apt install zenity`), or kdialog on
KDE; on the Mac, AppKit's open and save panels, with nothing to install.

```bash
ctest --test-dir build                      # silent; test-files saves and opens in a temporary folder
build/umpire-daw                            # a new, untitled session
build/umpire-daw ~/Music/Song.umpire        # with a saved one open
```

## The bundle

```
Song.umpire/
  session.json            Umpire's own: everything but the music
  clips/clip-7.midi2      one MIDI Clip File per clip, by its id
  plugins/track-2.state   a plugin output's state (its CLAP state), opaque
```

`session.json` (format `umpire-session`, version 1):

```json
{
  "format": "umpire-session",
  "version": 1,
  "tempoMap": {"tempos": [{"tick": 0, "bpm": 120.0}], "meters": [{"tick": 0, "beats": 4, "unit": 4}]},
  "loop": {"on": false, "start": 0, "end": 0},
  "tracks": [{"id": 2, "name": "Keys", "armed": false, "muted": false, "soloed": false,
              "clips": [{"id": 3, "clip": "clips/clip-7.midi2", "start": 0}]}],
  "outputs": [{"track": 2, "device": "<an Instruments::outputId>", "state": "plugins/track-2.state"}],
  "clips": [{"id": 7, "file": "clips/clip-7.midi2", "name": "Keys 1",
             "scale": {"edo": 12, "reference": 60}, "scaleFit": "workedOut", "scaleCandidates": [12]}],
  "next": {"track": 3, "trackNumber": 2, "clipRef": 4, "clip": 8}
}
```

- **Ticks are Umpire's** (40320 to a quarter), as integers.
- **The tempo map is a list of tempos and a list of meters**, so the tempo
  map (MILESTONES.md, T1) changes no format. Umpire has one of each for now:
  it writes one and reads the first.
- **A clip's file is named by its id**, and an edited clip gets a new id
  (milestone 10, clip-editor.md), so a file is always the same music. A
  new scale or name keeps the id: they're here, not in the file.
- **A clip reference names its clip by its file.** Two references to one
  clip share one file, and the clip comes back shared, as it was.
- **A clip's name and scale are in `session.json`**, not in its file. Its
  notes, timing and length are in the file.
- **`next` holds the controller's counters**, so a reopened session never
  reuses a track, reference or clip id (decisions.md, "Time, clips and
  pitch"). On reading, each is at least one past the highest in the file.
- **Outputs are kept as named**, a device or plugin that isn't there
  included, as the session keeps them (Session.h, `Output`).
- **Audio files** (prints, mixes: milestones 15 and 17) will be an `audio`
  list beside `clips`. A reader ignores fields it doesn't know.

A version Umpire doesn't know yet, a missing file or a broken one refuses
the whole session, saying which file and why. Nothing is half-opened.

## MIDI Clip Files

M2-116-U v1.0 (11 May 2023), with the messages from M2-104-UM v1.1.2. The
Mac's copy of M2-104 is v1.0, which has neither Delta Clockstamps nor Flex
Data. AMEI publishes both (ideas.md, "References"). A file Umpire writes:

| Words | What |
|---|---|
| `SMF2CLIP` | the File Header, 8 bytes |
| `00400000` `00309D80` | Clip Configuration Header: a Delta Clockstamp of 0, then DCTPQ 40320 |
| `00400000` `F0200000 0 0 0` | Start of Clip |
| `D0100000 t 0 0` | Set Tempo, `t` in 10 ns units per quarter, sharing Start of Clip's Delta Clockstamp (7.1.1) |
| `D0100001 nn020800 0 0` | Set Time Signature, n/4 (the denominator is a power of two, 2 for a quarter; eight 32nds to a quarter) |
| `004xxxxx` message ... | each event after the Delta Clockstamp of its tick; events at one tick share one |
| `004xxxxx` `F0210000 0 0 0` | End of Clip, at the clip's length |

- **Written at 40320 ticks to a quarter**, which DCTPQ's 16 bits hold, so
  a clip's ticks go out and come back exactly.
- **A gap longer than a Delta Clockstamp's 20 bits** (1,048,575 ticks,
  about 26 quarters at 40320) is restarted with a Null message (NOOP)
  after a full Delta Clockstamp, as 3.2.2 says, and the next counts from it.
- **The tempo and meter go out for other apps and are dropped on reading**:
  the session's tempo map rules Umpire's time. A clip played at another
  tempo keeps its ticks.
- **Reading takes any DCTPQ**, rounding to Umpire's ticks. Messages in the
  Clip Configuration Header (a program change, say) come first, at tick 0.
  Set Profile On, NOOPs, JR timestamps and stream messages are dropped.
  Nothing after End of Clip is read.
- **What's kept is what was recorded**: every message of a take, of any
  size (32 to 128 bits), in its order (decisions.md, "Time, clips and
  pitch": a take is stored as MIDI 2.0).

## Saving

- **Written whole, then swapped in**, as StageHand writes its racks: the
  bundle goes into `Song.umpire.tmp`, then the old one is renamed aside, the
  new one renamed in and the old one removed. A crash never leaves half a
  session, and a clip deleted since the last save leaves no file behind.
  Every save writes every clip. That's cheap at today's sizes (ideas.md,
  "Is size a real problem?" estimates 70 MB an hour at most). Writing only
  new clips can come once it's measured to matter: clips never change
  (decisions.md), so a clip's file never needs rewriting.
- **Plugin states are asked for at save time** (`AudioEngine::saveState`,
  CLAP's state extension). A plugin that isn't installed keeps the state it
  was opened with, so saving doesn't lose it. A state is saved only while
  its track's output is still that plugin.
- **"Modified" follows undo**: `History::state()` numbers each state the
  session has been in. Saving notes the current one, so undoing back to
  what was saved means nothing is unsaved. A plugin's own settings
  changing (in its editor) doesn't count as a change, since Umpire isn't
  told. Save saves them anyway.
- **A take still recording isn't saved**: it isn't a clip until recording
  stops.

## Opening

`TrackController::open` reads the whole bundle first, and changes nothing if
that fails. Then it stops the transport and goes back to the start, drops
any take in progress, closes every output and reopens them from the new
session, and clears undo. Each plugin output opens with its saved state,
given to the plugin before it's activated, as HostedPlugin asks
(`AudioEngine::open`'s `state`). Grid windows close, since their clips went
with the old session.

New is the same, with an empty session, as at launch.

## The UI

Agreed with the author before it was built, 2026-11-01.

- **File menu**: New (Ctrl+N), Open... (Ctrl+O), Save (Ctrl+S), Save As...
  (Ctrl+Shift+S), then Quit (Ctrl+Q). Cmd on the Mac.
- **The window title** is the session's name, "Untitled" until it's saved,
  with a • while something is unsaved: `Song • — Umpire`.
- **Before New, Open or Quit drops unsaved changes**, an ImGui prompt:
  Save / Don't save / Cancel. Enter is Save, Escape is Cancel. The window's
  close button and Ctrl+Q in a grid window are Quit too.
- **The dialogs** are StageHand's `FileDialog`, copied as the widgets were
  (decisions.md, "Milestone 4"): zenity or kdialog as a separate process,
  so the main loop never waits. On the Mac the same class is AppKit's
  NSOpenPanel and NSSavePanel (`FileDialogMac.mm`), begun modeless, so the
  loop goes on there too. Open chooses a `.umpire` folder, or the
  `session.json` inside it. Save As suggests `Name.umpire` beside the
  session and adds `.umpire` if it's left off. If there's already a session
  with that name, it asks before replacing it ("Replace it?"). Anything else
  already there is never replaced. The Mac's save panel asks about a name
  that's already there itself, so replacing a session there asks twice.
- **Errors** (no dialog tool, a broken file, a full disk) are said in the
  same prompt, with OK.

## What's in it

| Piece | Where | Toolkit |
|---|---|---|
| MIDI Clip File writer and reader | `src/App/ClipFile.{h,cpp}` | no |
| The bundle: save, open, paths, save targets | `src/App/SessionFile.{h,cpp}` | no |
| nlohmann/json 3.11.3, vendored as StageHand's | `external/nlohmann/` | no |
| `History::state()`: modified, undo-aware | `src/App/History.h` | no |
| `PluginState`; `AudioEngine::open(id, state)`, `saveState` | `src/App/Session.h`, `src/App/AudioEngine.*` | no |
| `Player::restore`/`closeAll`/`pluginState` | `src/App/Player.*` | no |
| `TrackController::save`/`open`/`newSession`/`modified`; the view's `sessionName`/`modified` | `src/App/TrackController.*`, `src/App/TrackViewModel.h` | no |
| `FileDialog`, copied from StageHand; AppKit's panels on the Mac | `src/App/FileDialog.{h,cpp}`, `src/App/FileDialogMac.mm` | no (AppKit on the Mac, as `MainLoop`) |
| File menu, keys, the prompt | `src/Ui/TrackPanels.*`, `src/Ui/TrackCanvas.cpp` | ImGui, DGL |
| The actions, the dialogs, the title, the close button as Quit | `src/Ui/MainWindow.cpp` | DGL |
| `umpire-daw SESSION` | `src/App/main.cpp` | no |
| Tests: clip files byte for byte, round trips, long gaps, another DCTPQ, broken files; bundles; save, open and new through the controller with a fake plugin's state | `tests/FileTests.cpp` (`test-files`) | no |

## Not in it (later)

- **A `.umpire` bundle as a Mac package**: Finder and the panels show it
  as a folder. Declaring it a document package in the app's Info.plist
  would show it as one file, and Open would then choose files, not
  folders.
- **Importing and exporting a single clip** (`.midi2` to or from a track)
  is not in the app's menus yet. `ClipFile` already reads other apps' files.
  The plugin has both (Export take..., Import clip...; plugin-version.md,
  "P4").
- **Recent sessions, autosave, and remembering the last folder between
  runs.**
- **Clip names and the session's name in the clip files** (Flex Data's
  MIDI Clip Name and Project Name). Leaving them out loses nothing, since
  `session.json` has them.
- **Writing only what changed**, as above, once a session's size makes it
  matter.
