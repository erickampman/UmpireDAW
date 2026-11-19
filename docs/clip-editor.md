# The clip editor (milestone 10)

The grid window edits its clip. Notes can be selected, moved in time and
pitch on the clip's EDO grid, resized, deleted, added and given a velocity.
Each edit is a named command on the session, so undo covers it, every
reference to the clip sees it, and it is saved. Built and tested on the
Linux PC on 2026-11-01. The UI (below) was agreed with the author before it
was built. Checking it by eye and by ear is the author's.

## Building and running

As before ([BUILDING.md](../BUILDING.md)):

```bash
ctest --test-dir build                     # silent; test-clip-edit and test-grid-model cover the edits
build/umpire-daw                           # double-click a clip to open its grid
build/umpire-daw --self-test /tmp/shots    # silent: the grid's checks now edit the recorded clip
```

## The UI

Agreed with the author on 2026-11-01. They chose the recommended option in
each case.

- **Selecting.** A click on a note selects it. Shift+click adds a note to the
  selection, or takes a selected one out (when let go without a drag: a
  Shift+drag slides instead, below). A drag that starts on empty grid draws a box
  (the accent, faintly filled) and selects the notes it touches; with Shift
  it adds them. A click on empty grid, without a drag, still adds a note, as
  it always has. A click on a note in a selection, without a drag, selects
  only that note. Selected notes have an amber edge, and the focus note (the
  one the inspector shows) has a thicker one. Edit > Select all notes
  (Cmd+A on the Mac, Ctrl+A on Linux; added 2026-11-19) selects every note,
  keeping the focus note, or making the last note it if there was none.
- **Moving.** A drag moves the whole selection by whole sixteenths, the
  columns the pointer crosses, and each note keeps its offset from the grid.
  A recorded note moved in time keeps its feel: moving isn't quantizing
  (decisions.md, "The clip editor"). Changed 2026-11-01 at the author's
  request: at first the focus note's start landed on the nearest sixteenth.
  A drag that only changes rows never moves a note in time. The selection
  stays inside the grid, as one block. Two modifiers, agreed with the author
  the same day:
  - **Shift+drag slides freely**, in ticks, without reference to the grid
    (as in DP, and as clip drags on the lanes). It works from the press: a
    Shift press on a note that then drags slides it (and selects it if it
    wasn't).
  - **Ctrl+drag (Cmd on the Mac) puts it on the grid**: the focus note's
    start lands on the nearest sixteenth and the rest of the selection moves
    with it. That's quantizing chosen notes on purpose, which the rule
    allows (decisions.md, "The clip editor").
- **Resizing.** The last 5 px of a note (a third of it if it's short) grab its
  end, and the pointer shows a left-right cursor there. The drag changes the
  selected notes' lengths by whole sixteenths, so their ends keep their
  offsets too. Shift: freely; Ctrl (Cmd): the focus note's end onto the
  nearest sixteenth. A note is at least a sixteenth long (or as
  long as it was, if shorter), and at least a tick when dragged freely.
- **Keys.** Up and Down move the selection a row, and with Shift an octave
  (the EDO's rows). Left and Right move it a sixteenth. Delete or Backspace
  deletes it. Ctrl+Z undoes; Ctrl+Shift+Z or Ctrl+Y redoes (Cmd on the Mac).
  The undo is the session's, shared with the track view. Each key press is
  one command.
- **Quantize notes** (Edit menu, and Q; added 2026-11-01 at the author's
  request): every selected note's start onto the nearest sixteenth, its
  length kept and its per-note data moved with it, as one "Quantize notes"
  command. That's quantizing chosen notes on purpose, as Ctrl+drag does for
  one: printing it, which the rule allows. The quantize effect, with a
  strength, is still to come (below).
- **Deleting:** Delete or Backspace deletes the selection; right-click
  deletes the note, or the selection it's in; and Edit > Delete notes.
- **Scrolling** (added 2026-11-01, after the author found no way to scroll
  through time): the wheel scrolls the rows, and Shift+wheel or a sideways
  swipe scrolls through time, a beat a notch. That's the track view's rule,
  and StageHand's sequence editor's. The keyboard at the left stays put.
- **Edit menu:** Undo and Redo, named ("Undo Move notes"), then Delete note
  (or Delete notes), then Delete all notes, which replaces Clear all. File >
  New clip is gone: the window is its clip's.
- **The inspector.** The velocity knob sets every selected note's velocity.
  The Pitch, Start and Length sliders move or resize the focus note, and the
  rest of the selection by as much. Start and Length show sixteenths and
  change by whole ones, keeping the note's offset. Each
  control shows its change as it's dragged and makes one command when let
  go, as the Tuning sliders do. "n notes selected" shows when there's more
  than one.
- **The clip's name** in the inspector is a "Rename clip" command when Enter
  or a click elsewhere ends the edit. What's typed used to be lost.

## The commands

`TrackController::editNotes(ref, label, changes, added)` makes the edit. Every
reference to the old clip gets the new one, as one command named for what
it did:

| Edit | Command |
|---|---|
| a drag, the arrows, the Pitch or Start slider | Move note / Move notes |
| an edge drag, the Length slider | Resize note / Resize notes |
| the velocity knob | Set velocity |
| a click on empty grid | Add note |
| Delete, right-click, the menu | Delete note / Delete notes |
| more than one kind at once | Edit notes |
| the name field | Rename clip (`renameClip`) |

An edit that changes nothing is no command; a drag that ends where it began
included.

The grid (`GridModel`) shows a gesture as it goes, as a preview in the
window, and `commit()` compares its notes with the clip's to say what
changed (`pendingChanges`). It then calls the window's `editClip`, which
calls the controller, and shows the new clip. The edited notes stay
selected: they're found again by where the edit placed their note-ons
(`EditedClip::placed`). A new scale or name keeps the selection, since the
music is the same. After an undo the selection is let go, as a clip that
undo takes away is.

## Decisions

- **An edited clip gets a new id.** A clip's file is named by its id
  (`clips/clip-<id>.midi2`), and a clip never changes once made
  (decisions.md, "Time, clips and pitch"). So a file is always the same
  music, and "write only new clips" (files.md, "Saving") stays possible.
  A new scale or a new name keeps the id: those are in `session.json`, not
  in the file, as `setClipScale` already did.
- **A grid window follows its clip by pointer, not by id**: one window per
  clip, whichever reference opened it. A double-click on another reference
  to the same clip brings that window forward.
- **What goes with a note** (`ClipEdit`): its note-on, its note-off, and the
  per-note messages for its key in between: per-note controllers, per-note
  pitch bend, Per-Note Management and poly pressure. Those sent for its key
  at its start, just before its note-on, count too. They move with it in
  time and are re-addressed if its key changes. When a note is shortened,
  what fell past its new end comes at its end, before its note-off; nothing
  is dropped. A Pitch 7.25 sent for its key earlier belongs to the key (a
  tuning table, playback.md) and stays where it was.
- **Pitch.** A time-only move or a resize keeps a note's exact pitch, even
  off its row ("+3 cents from its row" stays true). A pitch move puts it
  exactly on its new row's pitch in 7.9 (`GridModel::rowPitch79`): the next
  31-EDO step, not the nearest 12-EDO note. The new pitch is written the way
  the note came:
  - a Pitch 7.9 attribute (KeyboardEDO): the attribute, rewritten;
  - a Pitch 7.25 sent with it (Retune's way): that 7.25, rewritten;
  - a plain note number (a keyboard): a plain note number when the pitch
    is a whole 12-EDO note and that key is free.
  
  Otherwise the note gets a Pitch 7.9 attribute on the free note number
  nearest its pitch, as StageHand's sequences do. "Free" means no other note
  on that group, channel and key overlaps it.
- **Notes it didn't touch sound as before.** After every edit each note's
  pitch is checked with `notesOf`. A note that would now sound different
  gets a 7.9 attribute at the pitch it had: for example, a note on the same
  key whose Pitch 7.25 glide came from a deleted note, or a moved note now
  before its key's tuning. Tested.
- **Added notes** are MIDI 2.0 on the clip's first note's group and channel
  (group 1, channel 1 in an empty clip), with an attribute only when the row
  isn't a 12-EDO note. A note-on's velocity is at least 1.
- **Velocity.** The grid shows 7 bits and sets 16 (the 7-bit value scaled
  up), and only for the notes whose velocity changed. A MIDI 1.0 note from
  a file keeps its protocol when 7 bits say the value exactly; otherwise it
  becomes MIDI 2.0.
- **Time.** Notes are in ticks (40320 a quarter), so recorded timing
  survives until a note is moved in time. The grid's columns are sixteenths.
  It's as long as the clip in whole bars, and a note may go anywhere in it.
  The clip grows to end at its last note's end. The grid never shrinks while
  its window is open.
- **At one tick** the order is: a moved note's note-off (and what was
  clamped to its end), then the clip's other events, then a moved note's
  start (what came before its note-on, then the note-on). So a note ending
  where another starts on the same key doesn't cut it off.
- A note held to the end of a clip (no note-off) gets a note-off when it's
  edited.
- **Fixed on the way:** the lanes' note cache was keyed by a clip's address,
  so a new clip at a dropped one's address would have drawn its notes. It now
  checks the clip is the same one (a weak pointer) and drops entries for
  clips that are gone.

## What's in it

| Piece | Where | Toolkit |
|---|---|---|
| `editNotes`, `NoteChange`, `NewNote`, `EditedClip` | `src/App/ClipEdit.{h,cpp}` | no |
| `NoteSpan::on`/`off`: a note's events | `src/App/Clip.{h,cpp}` | no |
| `TrackController::editNotes`, `renameClip` | `src/App/TrackController.*` | no |
| `GridModel`: ticks, selection, drags, `commit`, `pendingChanges` | `src/App/GridModel.*` | no |
| Selecting, the box, moving, resizing, keys | `src/Ui/GridCanvas.*` | DGL |
| Edit menu, the inspector's commands, the name | `src/Ui/GridPanels.*` | ImGui |
| The window following its clip, undo | `src/Ui/MainWindow.cpp` | DGL |
| Tests: edits, per-note data, pitch, keys, velocity, the grid's commands, two references, undo, save and reopen | `tests/ClipEditTests.cpp` (`test-clip-edit`), `tests/GridModelTests.cpp` | no |
| Self-test: the grid's input checks now edit the recorded clip (add, rename, delete, move, the menu, velocity, a resize, a box, Shift+click, nudges, undo and redo keys) | `src/Ui/GridSelfTest.cpp` | DGL, ImGui |

## Checked

On the Linux PC (GCC 15.2, X11), 2026-11-01:
- `ctest --test-dir build`: all 7 suites pass, `test-clip-edit` new.
- `build/umpire-daw --self-test DIR`: every grid check passes, the new ones
  included. The screenshot `07-box.ppm` shows the box selecting two notes.
  The track view's first idle check (frames while idle, before any grid
  code runs) failed in two of three runs (2 and 21 frames, with no view
  change and no MIDI) and passed in the third. That is the same kind of
  flake recording.md records, most likely the desktop drawing into the
  window.

## Not in it (later)

- Copy, cut, paste and duplicate.
- **Quantize** is an effect, not an edit (decisions.md, "The clip editor";
  ideas.md, "Quantize as a plugin"). When it exists, the grid still edits
  the recorded notes, shows that the effect is on (a badge), and marks where
  each note will play. Printing it is an edit through `editNotes`, which
  already moves a note's per-note data with it.
- **The grid's divisions** (the author, 2026-11-01: not sure the grid should
  always be sixteenths): to come back to. Choices include a note value per
  window or per clip, triplets and other tuplets (40320 ticks divide them
  exactly), and divisions that follow the zoom. Moves already keep offsets,
  so a coarser or finer division changes only how far a step goes.
- A velocity lane, and drawing per-note expression (ui-direction.md: only
  when the notes have it).
- Moving a note's left edge (the start, keeping the end).
- Editing notes in the lane by zooming (ui-direction.md, "Zoom replaces
  opening things"), which will share this code.
- Keeping the selection across an undo.
- Choosing a key whose Pitch 7.25 already sounds the new pitch, instead of
  an attribute, for a clip tuned by a table.
