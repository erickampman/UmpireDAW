# Playback to MIDI outputs (milestone 7)

A track plays its clips to a MIDI device while the transport runs, and what's
played live goes through to the touched track's device. The transport loops
and locates; on play, a locate and a loop wrap, notes sounding end and each
key's Pitch 7.25 is chased, since the synths keep it as a tuning table. Built
and self-tested on the Linux PC on 2026-10-29. **Not yet built on the Mac**,
and nothing has been heard yet: playing to a real synth is the author's.

## Building and running

As milestone 6 ([BUILDING.md](../BUILDING.md)), with uw-core at `9f4ee51` or
later (scheduled output, below):

```bash
ctest --test-dir build                     # silent: now with test-playback
build/umpire-daw                           # the track view
build/umpire-daw --self-test /tmp/shots    # both windows, scripted (silent; it never chooses an output)
```

To hear it: touch a track, choose its output under its name (a hardware
synth, or a software one such as FluidSynth or a plugin host's MIDI input),
and play. A running synth keeps the tuning Umpire last sent it; restart it to
start clean.

## What's in it

| Piece | Where | Toolkit |
|---|---|---|
| `MidiOutput::sendAt`, `schedules`, `cancelScheduled`; ALSA's on a sequencer queue | uw-core `src/uw/midi/` | no |
| `Output`: a track's edge to a device; the loop | `src/App/Session.h` | no |
| `Transport`: the loop, raw ticks, runs | `src/App/Transport.h` | no |
| `MidiOutputs`: the platform output, its choices, a handle per stream | `src/App/MidiOutputs.*` | no |
| `Player`: scheduling, ending notes, the Pitch 7.25 chase, thru | `src/App/Player.*` | no |
| `TrackController`: `setOutput`, `setLoop`, `setLooping`, the player's calls | `src/App/TrackController.*` | no |
| The output choice, the Loop button and menu item | `src/Ui/TrackPanels.*` | ImGui |
| The loop on the ruler, dragging it, the L key | `src/Ui/TrackCanvas.*` | DGL |

### Scheduled output (uw-core)

The main loop ticks every 10 ms, and a frame can take longer, so sending each
event when the loop gets to it would put it up to a tick or more late, and
unevenly. Instead uw-core's output seam can send at a time
(`MidiOutput::sendAt`, on `nowNanoseconds()`'s clock), and the platform holds
the message until then:
- **ALSA:** a sequencer queue of each sending client's own, scheduled in real
  time. The queue's time is read with every call and compared with ours, so
  the two can't drift. Each opened handle tags its events with a number of its
  own, and `cancelScheduled(handle)` removes that handle's (a stop, a
  locate). On the Linux PC the queue runs on the high-resolution timer;
  uw-core's test schedules 50 and 150 ms ahead through a loopback and they
  arrive within a millisecond of their times.
- **Elsewhere** (CoreMIDI, until uw-core's M6 item is done): `schedules()` is
  false and `sendAt` sends now. The player then sends each event on the tick
  it falls in, so on the Mac playback is up to 10 ms late for now.

### Outputs

- **Where a track plays is an edge, not a field of the track**
  (`Session::outputs`, ui-direction.md: the model is a graph): a track to a
  device, by its id ("client name:port name" on ALSA). One per track for now;
  a plugin is another kind of end with milestone 8. Setting it is a command
  ("Set output", "No output"), so undo covers it, and removing a track
  removes its edge.
- **The choices** are the platform's destinations, hardware first, as the
  inputs' choices; Umpire's own inputs aren't offered (a track playing into
  them would loop). Umpire's output is its own pair of ALSA clients, "Umpire
  Out" and "Umpire Out MIDI 2.0", beside the inputs' "Umpire".
- **A device that isn't there** keeps the edge: the row shows its id with
  "(not connected)" in red, and when it's connected the track plays to it.
- **One handle per track**, even for one device: cancelling a track's
  scheduled events (muting it, say) leaves the other tracks' alone.
- **A MIDI 1.0 device gets MIDI 1.0**: clips are MIDI 2.0, downconverted by
  uw-core's `downconvert`, which drops what MIDI 1.0 can't say (per-note
  data, Pitch 7.9 and 7.25 among it). Turning per-note pitch into channel
  messages for a MIDI 1.0 device (ideas.md, 23) is later.

### The player

- **Each tick**, after following the session, each audible track with an
  output sends its clips' events from where it got to up to 100 ms ahead
  (`Player::kLookahead`; up to now where the platform can't hold them), each
  at its time. A clip plays its events from its start to its end, the note-off
  a take ends with included.
- **Audible:** not muted, and soloed if any track is. A track whose output,
  audibility or clips change while playing ends its notes and joins in again
  from the playhead (with Pitch 7.25 chased); the other tracks go on.
- **Stop and locate** drop what's scheduled ahead and end the notes sounding
  then (a note-off each); a locate while playing chases at the new position
  and plays on from there.
- **A tempo change, the loop changed, or recording starting or stopping**
  while playing gives the transport a new anchor: what was scheduled ahead is
  dropped and sent again at the new times, and notes sounding go on (they get
  their note-offs at the new times).
- **Played live**, after upconversion (milestone 6), goes through to the
  touched track's output, now, unless the track is muted (solo doesn't stop
  it). A note-off goes where its note-on went, even if another track has been
  touched since; a track whose output changes or goes ends the live notes held
  on it.

### Chasing Pitch 7.25

The synths keep each key's Pitch 7.25 as a tuning table: it lasts until
changed or reset, whether or not a note is playing. So playing from anywhere
but the start would use whatever tuning was sent last. On play, a locate, and
a loop wrap (at the wrap's time, before the loop start's own events), for
each key (group, channel, note) that any of the track's clips tunes:
- the Pitch 7.25 last sent for it before the playhead, as recorded;
- or, where none is in force there (none yet, or a Per-Note Management reset
  since), a Per-Note Management reset, so the synth drops a tuning left from
  elsewhere in the song.

Keys the clips never tune aren't touched. Nothing else is chased yet:
controllers, programs, pitch bend.

### The loop

- **Part of the session** (`looping`, `loopStart`, `loopEnd`), changed by
  commands ("Set loop", "Loop on", "Loop off"), so undo covers it and saving
  (milestone 9) will keep it. The transport follows the session's, as it does
  the tempo.
- **A drag along the ruler** sets it, to the beat, and turns looping on; a
  click still moves the playhead. The **Loop** button after Record, the
  Transport menu's Loop and the L key turn it on and off; turning it on with
  no range set loops the first bar. On the ruler it's an amber band while
  looping, grey when off.
- **The transport counts raw ticks** from its anchor, which never jump; the
  position is a raw tick wrapped by the loop, and the player plans in raw
  ticks (`Transport::runs`), so a wrap within the lookahead is scheduled like
  anything else. At a wrap, the notes sounding end and Pitch 7.25 is chased.
- A playhead before the loop's end goes back when it gets there; one past it
  plays on.
- **Not while recording:** recording plays straight through (loop recording,
  with takes per pass, is later).

## Checked

On the Linux PC (GCC 15.2, X11, kernel 7.0, the ALSA sequencer's HR timer),
2026-10-29:
- uw-core: no warnings; `test-alsa-output` sends 50 and 150 ms ahead through
  a loopback (arriving +0 ms, five runs), a time already past at once, and
  cancels one handle's without touching another's to the same port; all nine
  suites pass.
- Umpire: no warnings; `test-playback` (34 checks over a fake output and
  clock: the loop's runs and wrap, no wrap while recording, the choices
  without Umpire's own input, the output as a command, through to the touched
  track, each note at its time and once, stop and locate ending notes and
  dropping what was ahead, a MIDI 1.0 device getting MIDI 1.0, undo, mute and
  solo while playing, the loop wrapping with notes ended and replayed,
  Pitch 7.25 chased on play at three positions and on a wrap, a note-off
  following its note-on to the track it went to, a muted track playing
  nothing through, a tempo change retiming what's ahead without repeating a
  note, a device connected later, removing a track and undoing it), and the
  other three suites pass.
- `--self-test` at scale factors 1, 1.5 and 2: the new checks (a ruler drag
  setting the loop, L, the Loop button, the playhead wrapping while playing,
  the touched row's output choice opening and closing with nothing chosen)
  and all the old ones pass. At 1, four runs of six failed one or two of the
  idle checks (frames drawn while nothing was being done; once "2 MIDI
  messages arrived", once the activity light didn't go out), in the track
  view and in the grid window, which this milestone didn't touch: real input
  reaching the test (the KeyLab 88 is connected), as milestone 6 found. Every
  playback, loop and output check passed in every run.
- Screenshots: `t06-loop.ppm` (the loop band over bars 2 and 3, the Loop
  button lit), `t07-output.ppm` (the output choice open: No output, then the
  KeyLab 88 and Midi Through).

By the author, 2026-10-29, on the Linux PC: a track played to StageHand (its
ALSA input) and was heard.

## Still to do, by the author

- **Listen:** a MIDI 1.0 synth and a hardware one; a KeyboardEDO 31-EDO take
  played back to a synth that takes Pitch 7.25, started from the middle, and
  looped.
- Whether the output choice, the Loop button and the ruler's band look right.
- **The Mac:** build and run (CoreMIDI sends at once, so no lookahead there
  until uw-core's CoreMIDI scheduling).

## Not in it (later)

- Plugins as outputs (milestone 8), and a clip's own instrument
  (ui-direction.md).
- Chasing anything but Pitch 7.25 (controllers, programs, pitch bend), and
  notes held across the playhead (a note that started before it doesn't
  sound).
- Per-note data as channel messages for a MIDI 1.0 device (ideas.md, 23).
- MIDI clock or MIDI Time Code out, a count-in, a metronome, loop recording.
