# Transport, clips and recording (milestone 6)

The track view gets a transport and records what's played into clips, under
the UI direction (ui-direction.md): input belongs to the session, rows show
only what isn't the default, and every change can be undone. Built and
self-tested on the Linux PC on 2026-10-23. **Not yet built on the Mac**, and
nothing has been recorded by hand from real MIDI.

## Building and running

As milestone 5 ([building.md](building.md)):

```bash
ctest --test-dir build                     # silent: the grid model, the track controller
build/umpire-daw                           # the track view
build/umpire-daw --self-test /tmp/shots    # both windows, scripted (silent; don't touch them or play MIDI meanwhile)
```

## What's in it

| Piece | Where | Toolkit |
|---|---|---|
| `Clip`, `ClipEvent`, `ClipRef`, `NoteSpan`, `notesOf`, ticks | `src/App/Clip.*` | no |
| `Scale`, `workOutScale`: a clip's scale from its notes | `src/App/Scale.*` | no |
| `Session`, `Track` | `src/App/Session.h` (was `Track.h`) | no |
| `History`: undo and redo | `src/App/History.h` | no |
| `Transport`: play, stop, record, locate, tempo | `src/App/Transport.h` | no |
| `TrackController`: commands, routing, takes | `src/App/TrackController.*` | no |
| The transport bar, sparse rows, status line | `src/Ui/TrackPanels.*` | ImGui |
| Clips, the playhead, the prompt | `src/Ui/TrackCanvas.*` | DGL |
| A clip's grid window, its tuning | `src/Ui/MainWindow.cpp`, `src/Ui/Grid*`, `src/App/GridModel.*` | DGL, ImGui |

### Time and clips

- **Ticks:** 40320 to a quarter note (decisions.md, "Time and clips"), in a
  64-bit `Tick`. Seconds come from the tempo; nothing stores them.
- **A clip** is UMP messages with ticks from its start, and a length. It never
  changes once made: references and undo's snapshots share it. A track holds
  `ClipRef`s: a clip and where it starts. Transforms (ideas.md, 9) and a
  clip's own instrument go on the reference later.
- **What's stored:** every channel voice message the take saw, as MIDI 2.0.
  MIDI 1.0 is upconverted by uw-core's `upconvert`, with one converter per
  input that runs whether or not anything records, so RPN and bank selections
  made before recording are followed. Per-note controllers, Pitch 7.25 with no
  note, and Per-Note Management are kept as they came. System messages
  (clock), SysEx and stream messages aren't recorded yet.
- **A note's pitch** (`notesOf`): its Pitch 7.9 attribute; without one, the
  Pitch 7.25 last sent for its key, until a Per-Note Management reset;
  without that, its note number. In 7.9, so no tuning is assumed: the lanes
  draw notes at their pitch, and the grid places them on its EDO's rows.

### The transport

- In the menu bar from the lanes' edge: to start, stop, play (lit while
  playing), record (lit while recording), the position as bar.beat.sixteenth
  and m:ss.t, and the tempo (drag, or Ctrl+click to type; one undoable change
  when let go). A Transport menu has the same.
- The position runs from an anchor on uw-core's clock, the one MIDI capture
  times use, so a message lands on the tick it was played at, not when the
  main loop got to it.
- The view's position moves about 30 times a second while playing, not every
  10 ms tick, and each move draws one frame (ImGui is owed three after
  input). The self-test checks 15 to 45 frames a second while playing.
- Playing past the right edge turns the page.
- Keys: Space plays and stops, Home goes to the start, Ctrl/Cmd+R records. A
  click on the ruler moves the playhead to the nearest beat.

### Recording

- **Record** starts playing if stopped, and starts a take on every armed
  track at the playhead. Pressing it again punches out and keeps playing;
  Stop ends the takes too. A locate while recording ends the takes and starts
  new ones from there.
- Tracks armed or disarmed while recording don't change the takes in
  progress: the takes are the tracks armed when recording started.
- **A take grows on its lane** as it's recorded (red, with its notes), and
  becomes a clip when it ends: all the takes in one "Record" command. A take
  with nothing in it leaves nothing. A note still held ends with the clip
  (a note-off at its end). A clip is named after its track and a number
  ("Keys 2").
- Recording with nothing armed says so in the status line, in red.

### Input belongs to the session

- Umpire listens to every input it opens (milestone 5's rule: the virtual
  inputs and every hardware input). Milestone 5's per-track input, channel
  and group choices are gone, and so are those fields of `Track`;
  `MidiSourceRef` and the input choices stay in `MidiInputs` for a filter
  asked for later.
- **Playing goes to the touched track** (its light), and while recording to
  every armed track too. The monitor's last column says which.
- **The status line** (above the monitor) names what's listening and where
  playing goes.
- **An empty session shows only the prompt:** "Play something to start", or
  Ctrl/Cmd+T. The first note played makes the first track, as the prompt
  says. A new session is empty.

### Rows: only what isn't the default

- An untouched row is the light, the name, and badges for armed (R), muted
  (M) and soloed (S), right-aligned. Nothing else.
- The touched row (the accent down its edge) shows its controls in place: the
  name to type, R, M and S, and its output ("No output" until milestones 7
  and 8). Clicking a row, or its lane, touches it; Up and Down move.

### Undo

- Every change to the session is a command with a name: add, remove, rename
  a track; arm, mute, solo; tempo; record; move and delete a clip. Edit >
  Undo and Redo name them; Ctrl/Cmd+Z undoes, Shift with it (or Ctrl+Y)
  redoes. A command that changes nothing isn't recorded.
- The touched track, the selected clip and the playhead aren't the session:
  undo doesn't move them, except that a touched track or selected clip that
  undo takes away is let go.
- Track and clip IDs and "Track n" numbers live outside the snapshots, so
  undo never makes one be reused.
- The history keeps 1000 commands.

### Clips on the lanes

- A clip is a teal box with its name and its notes as dashes, at least an
  octave tall; the selected one has the accent round it. A click selects it
  (and touches its track); a drag moves it along its lane to the nearest beat
  (Shift: freely), as one "Move clip" command; Delete or Backspace (or Edit >
  Delete clip) deletes it.
- **A double click opens it in the grid**, in its own window, one per clip
  (opening an open one brings it forward). The test grid window and its View
  menu item are gone.

### The grid's tuning

The grid is still milestone 4's, in sixteenths, and its edits stay in the
window: milestone 10 makes it edit the clip. What changed is pitch:
- **Rows are EDO steps**, as StageHand's sequence editor (its
  docs/sequences.md, "EDO pitch"): a tuning of an EDO (5 to 72) and the note
  step 0 sounds, set in the inspector's Tuning section. The rows cover the
  MIDI range; at 12-EDO, row n is note n.
- A clip's notes land on the row nearest their pitch (above). A retuned grid
  shows the clip again, until its notes are edited; after that each note keeps
  its pitch on the new tuning's nearest row.
- Rows are dark where their pitch rounds to a 12-TET black key (StageHand's
  rule: in 31-EDO the 2+3 pattern shows as bands). Each octave from the
  reference is labelled with its note name.
- **The clip holds its scale** (decisions.md, "Time, clips and pitch"),
  worked out from its notes as a take becomes a clip (below), and the grid
  opens in it, centred on the clip's notes. The Tuning section says how it
  came about: worked out, set by hand, "Not sure: 23, 31, ... fit" (too few
  notes), or "No EDO fits" (notes on the nearest rows of 12-EDO).
- **Changing it in the grid changes the clip's.** A slider previews as it's
  dragged; letting go makes one "Set scale" command, a new clip (same id,
  shared by every reference to the old one). Undo puts the old scale back,
  and the open grid follows either way. A retune keeps the pitch that was in
  the middle of the view there.

### Working out a clip's scale

`workOutScale` (ideas.md, "Working out a clip's scale"), built the same day
after the author's 31-EDO take from KeyboardEDO:
- Every EDO from 5 to 72 is tried: it fits when every note's pitch is a whole
  number of its steps from the others, and its lattice meets a whole MIDI
  note (KeyboardEDO anchors at a 12-EDO pitch), within 1.5/512 of a semitone
  (7.9's rounding, with room for a sender that truncates).
- 12-EDO wins whenever it fits (a whole-tone tune fits 6 too). Otherwise the
  EDOs that fit, leaving out multiples of a smaller one, are the candidates:
  one is worked out; several is "unsure", the smallest chosen for the user to
  confirm; none is "no fit", 12-EDO from 60.
- The reference is the MIDI note on the lattice nearest 60: 57 for 31-EDO
  from A (KeyboardEDO's default), 64 from E; for an EDO sharing a factor with
  12 (24), 60 itself.
- How sure, measured over random phrases in every EDO from 5 to 72 (except
  12's divisors and multiples), 50 each: two notes right 92%, unsure 6%;
  three, right 99%; four, 99.7%; six or more, every time. The misses are
  phrases whose notes are all 12-EDO pitches, where 12-EDO is a fair answer.
- Every scale is an EDO for now (decisions.md); pitch bends don't count.
## Checked

On the Linux PC (GCC 15.2, X11), 2026-10-23:
- no warnings; `test-track-controller` (44 checks: the inputs listening, the
  empty session, the first note making a track, playing to the touched track,
  commands and undo, recording, upconversion, Pitch 7.25 and Per-Note
  Management kept, held notes ended, punching in and out, recording with
  nothing armed, moving and deleting clips, tempo, removing and restoring
  tracks, then a 31-EDO take's scale worked out and set by hand: 49 checks
  now), `test-grid-model` (with 31-EDO clips, retuning, and pitch from 7.9,
  7.25 and a reset) and `test-scale` (12, whole-tone, 31 from A and from E,
  19, 24, 53, octaves only, one note, no fit, truncated pitches) pass;
- `--self-test` at scale factors 1, 1.5 and 2: the track view's checks (the
  empty prompt, playing to make a track, the touched row's controls and the
  untouched row's lack of them, undo and redo keys, the Edit menu, recording
  three notes with the record key and the take growing, frames while
  playing, Space and Home, selecting, dragging, undoing, deleting and
  restoring a clip, a ruler click), then a recorded clip's grid window,
  opening in its worked-out scale, following a scale chosen in it and an
  undo of that (screenshot `g00-31edo.ppm`: the clip at 31-EDO from A),
  showing its notes and milestone 4's grid checks. All pass.
- The screenshots looked right: the prompt, the sparse rows with an M badge,
  the red take, a selected clip.
- A bug found on the way: the canvas's `rgb()` gave DGL's `Color` alpha as
  0-255, which it takes as 0-1, so the armed lane's "faint" red wash
  (milestone 5) was solid.
- One self-test run failed its idle and drag checks with the transport
  running and no MIDI arriving: most likely a real key reached the window
  (Space) while it had focus. The idle check now says how many times the view
  changed and how many MIDI messages arrived.

## Still to do, by the author

- **Build and run on the Mac.**
- Record by hand: a keyboard (MIDI 1.0) and KeyboardEDO into "MIDI 2.0 In",
  punching in and out. (A 31-EDO take from KeyboardEDO, 2026-10-23: the grid
  opened at 12-EDO, and set to 31 from 60 it's still a quarter step off,
  because KeyboardEDO's 31-EDO steps are anchored at A. Hence decisions.md,
  "Time, clips and pitch": the clip holds its scale, worked out from the
  notes.)
- Whether the transport, the badges, the status line and the clips look
  right.

## Not in it (later)

- The command palette and task surfaces (ui-direction.md): not scheduled yet.
- Playback (milestone 7): mute and solo change nothing yet, and the playhead
  plays nothing.
- Loop recording, count-in, a metronome, meters other than 4/4.
- Saving (milestone 9): the tempo and the clips live only as long as the app.
