# Ideas

Candidate ideas for Umpire, kept for discussion. Nothing here is decided.
These were distilled from an earlier set of proposal documents (2026-10-07),
which were deleted afterwards. The factual errors in those documents are
corrected here.

The goal is UI and data representations that are as fresh as possible, given
how many DAWs already exist. Each idea notes its prior art, since that is the
real test of whether it is new.

## The freshest

1. **Place notes by the pitch you hear, not by note number.** MIDI 2.0's
   per-note pitch bend, the registered per-note controller Pitch 7.25 (#3),
   and the Note On attribute Pitch 7.9 (type 0x03) let a note's real pitch
   sit between keys. The roll draws it where it sounds, so microtonal music
   and glides are placed correctly. Prior art: Bitwig and others show pitch
   expression inside a note, but still position the note by its note number.
   Open question: how attribute, per-note controller and per-note pitch bend
   combine.
2. **Editors built from what the device declares.** A device that declares a
   Profile (organ, drums, ...) or names its parameters through Property
   Exchange gets an editor built from that: drawbars for an organ, a pad grid
   for drums, real parameter names for a synth. Prior art: device panels are
   made by hand or come from templates. A display-only first version is
   cheap. Caveats: hardware support is uneven, and profiles are coarse.
3. **Procedural clips.** See [Procedural clips](#procedural-clips) below.
4. **Projects stored by content, like git.** Clips are named by a fingerprint
   of their contents, which gives deduplication, diffs between project
   versions, and branching takes or versions. Prior art: none known in DAWs.

## Fresh-ish

5. **Groups as the routing backbone.** A track is a view over a range of UMP
   groups or a device's function block, not a thing that owns a port. Routing
   screens show the names devices advertise ("Keyboard", "Drawbar Section").
   This is the difference between building on UMP and putting a MIDI 1.0
   design in a UMP wrapper. Groups are cheap: one clip can carry several
   parts without a port per part.
6. **A text view of the same model.** A tracker-style text rendering serves
   three purposes: screen-reader accessibility (REAPER with OSARA is about
   the only current answer), text snapshots for tests, and readable debug
   dumps. Prior art: trackers, but none is a second view onto a DAW's model.
7. **Zoom levels that match storage levels.** Continuous zoom from sections to
   bars to notes to per-note expression, where each level reads only the
   matching level of the storage index (section index, page index, events).
   Risk: objects that change form as you zoom can disorient people.
8. **Orientation as a view setting.** The model never stores which way time
   runs. This is not vertical *editing*. Vertical display is for chunked
   overviews where top to bottom reads naturally (song sections, setlists,
   lists of clips or scenes). Prior art: player-piano rolls, trackers.

## Known, but the right foundation

9. **The arrangement as a small list of clip references, with transforms on
   the reference.** The arrangement is small, mutable and always in RAM, and
   points to clips that are large and immutable. Transpose, quantize,
   humanize and velocity scaling are settings on the reference and never
   touch the notes. Undo, autosave and looping become cheap (a clip played
   200 times is one clip and 200 references), and prefetch is trivial.
   Prior art: Logic's region parameters, aliases and shared copies in Logic
   and Cubase. Making it the core structure is the new part, and ideas 3, 4
   and 7 depend on it.
10. **Per-note expression as first-class curves**, rather than thousands of
    sampled events. Smaller, and closer to what the player meant. The
    standard clip format stores events, so curves would be kept alongside the
    flattened events as extra data.

Ideas 1, 2 and 5 can only exist in a DAW built on MIDI 2.0, so established
DAWs can't easily copy them. They are the strongest reason for Umpire to
exist.

## Procedural clips

A clip that stores a recipe instead of notes, and produces exactly the same
notes from it every time. It sits between recorded notes (editable note by
note, but the clip doesn't know how they came about) and a live MIDI effect
(settings stay adjustable, but you can't edit individual notes, and output
may vary on each playback).

Example, an arpeggio clip:

- **Generator:** arpeggiator, version 2
- **Settings:** up-down, sixteenths, two octaves, C–E–G for 8 bars
- **Seed:** 1234, for any randomness (velocity variation, skipped notes)
- **Exceptions:** hand edits, such as delete the 47th note, set the 12th
  note's velocity to 110, add a note at bar 5

Playback runs the generator and then applies the exceptions. The piano roll
shows ordinary notes, and editing one records an exception.

What it gives you:

- Settings stay editable after hand edits (change the rate or chord, keep the
  edits where possible). Live 12's generators write plain notes once and lose
  the recipe.
- Repeatable, adjustable randomness, for example a seeded humanize: the same
  seed gives the same take, a new seed gives a new take, and the amount can
  be changed later.
- Small files: a ten-minute texture or per-note vibrato is a few hundred bytes
  of recipe.

The hard parts:

1. **Exact repeatability** on every machine and release: a fixed random-number
   generator and no unpredictable floating-point code.
2. **Versioning generators.** Songs made with v2 keep using v2. Old generators
   can never be removed.
3. **Keeping edits attached.** "The 47th note" breaks when settings change.
   Edits need a stable anchor, such as beat position plus pitch, and some
   edits won't survive a big change. This is the hardest part.
4. **Export.** The standard clip format stores events, so exporting flattens
   the clip, with the recipe kept alongside as extra data.

## Design principles worth keeping

- **Store UMP; translate only at the edges.** Translate to MIDI 1.0 on the way
  out to legacy devices, never on the way in.
- **Plan for capability discovery to be missing.** Every feature that relies
  on MIDI-CI or stream messages needs a path for devices that never answer.
- **Seeking must give a correct playback state.** Use checkpoints ("anchors")
  that hold full channel state, the set of sounding notes and per-note state,
  then replay from the nearest one. Place anchors by time as well as by event
  count, because long silences have no events. When jumping during playback,
  send note-offs for the notes Umpire started. Don't rely on all-notes-off or
  panic, which devices handle inconsistently and which also cut notes the
  performer is holding.
- **Separate indexes for viewing and playback.** The view asks which notes
  intersect a window (an interval query: list per page of notes that start
  there, plus notes carried over from earlier pages). Playback wants time
  order with lookahead.
- **Keep the compression format out of the document model.**
- **One kind of novelty per milestone.** A new UI, a new storage engine and a
  new file format are three projects.

## Timebase and format facts

- MIDI 2.0 channel voice is 64 bits (8 bytes). MIDI 1.0 channel voice in UMP
  is 32 bits (4 bytes), against 3 bytes in MIDI 1.0 byte streams.
- **Delta Clockstamp:** a 20-bit tick delta. **DCTPQ** (ticks per quarter
  note) is 16 bits, so the maximum is 65,535 PPQ. Check whether consecutive
  Delta Clockstamps add up (if they do, there is no limit on gaps).
- A 16-bit PPQ can handle tuplets: 36036 = 4 × 9009 divides by 2, 3, 4, 6, 7,
  9, 11, 12 and 13 (but not by 5 or 8).
- **Jitter Reduction timestamps** are 16-bit, in units of 1/31250 s, and wrap
  about every 2.1 s. They are for the wire only and can never be a document
  timebase.
- **Flicks** (1/705,600,000 s = 2^9 · 3^2 · 5^5 · 7^2) divide exactly by
  common sample rates and frame rates, which suits audio later. They are not
  exact against arbitrary tempos. A possible model: musical position stored
  as ticks is canonical, absolute time in flicks is derived and cached, and
  the two are never converted back and forth lossily.
- **MIDI Clip File (M2-116-U)** holds one clip, not a session, so the session
  container is Umpire's to define. Decide explicitly which format is
  authoritative. Check whether the MIDI Association is working on a
  multi-track container.
- Flex Data (tempo, time and key signature, text) replaces SMF meta events.

## Open questions and cheap experiments

- **Is size a real problem?** Rough estimates for MIDI 2.0 data at 8 bytes per
  message: MPE-style expression about 70 MB/hour, dense orchestral control
  data about 350 MB/hour. All of that fits in RAM, and it should compress
  well. Measure before building any streaming storage. Experiments: record an
  hour from real gear (bytes, message mix, share of MIDI 1.0-in-UMP,
  compressed size), and generate a synthetic 2 GB session and time a naive
  open, seek and edit.
- **The seek test:** seek to 1,000 random positions and check that the
  sounding notes match a replay from the start, exactly. This is the most
  valuable correctness test.
- **What really produces MIDI 2.0 data?** Much "MIDI 2.0" gear sends MIDI 1.0
  messages wrapped in UMP. Find out what's on the desk, including our own
  tools, before relying on the difference.
- **How does Umpire relate to StageHand** (which hosts CLAP plugins with MIDI
  2.0 support): as a sound engine, as shared code, or kept separate?
- **Transport on Linux:** raw ALSA UMP (sequencer UMP support since kernel
  6.5), [libremidi](https://github.com/celtera/libremidi), or PipeWire
  (internal MIDI moved to UMP in 1.4; confirm this). JACK has no UMP support.

## References

- UMP and MIDI 2.0 Protocol, M2-104-UM:
  [AMEI copy](https://amei-music.github.io/midi2.0-docs/amei-pdf/M2-104-UM_v1-1-2_UMP_and_MIDI_2-0_Protocol_Specification.pdf)
- MIDI Clip File, M2-116-U:
  [midi.org](https://midi.org/midi-clip-file-specification-smf-midi-2-0),
  [IANA](https://www.iana.org/assignments/media-types/audio/midi-clip)
- [ktmidi notes on MIDI 2.0 formats](https://github.com/atsushieno/ktmidi/blob/main/docs/MIDI2_FORMATS.md)
- ALSA UMP structs:
  [DC](https://www.alsa-project.org/alsa-doc/alsa-lib/struct__snd__ump__msg__dc.html),
  [DCTPQ](https://www.alsa-project.org/alsa-doc/alsa-lib/struct__snd__ump__msg__dctpq.html)
