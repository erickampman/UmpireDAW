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

## From the dynamic UI spec

From [daw-dynamic-ui-spec.md](daw-dynamic-ui-spec.md) (2026-10-23), kept as a
reference; where they would fit is in [ui-direction.md](ui-direction.md).

- **A routing view.** The session's graph drawn by stage, with a trace of
  what feeds a node and where it goes, and checks for loops and orphans
  (with a way to mark a loop as intended). 2D first; the spec's 3D depth
  view is an experiment. Worth building after instruments (milestone 8) and
  groups (milestone 12).
- **A word-pair XY controller.** A pad whose axes are word pairs ("Boxy ↔
  Airy") over an editable mapping to parameters: CLAP parameters, MIDI 2.0
  controllers on outputs, or what a device names through Property Exchange.
  MIDI learn on X and Y; the puck can be recorded into clips.

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

## Voice assignment by the controller

Added 2026-10-22. The controller (KeyEDO, or another program) decides which
synth voice each note uses, so that slew between notes can be chosen
algorithmically: "this note takes over voice 3, which was playing E, and
glides from there", or "a new voice, no glide". The synth still does the
glide itself, so it stays smooth and costs one message per note. The
alternative, the controller holding one note and sending a stream of Pitch
7.25 or per-note pitch bend, needs no voice concept, but takes many messages
and is only as smooth as their rate.

Prior art: MPE gives each note its own channel, which a synth can treat as a
voice, but glide stays the synth's own rule. No standard MIDI 2.0 message
names a voice that we know of.

Two ways to carry the voice number in MIDI 2.0:
- **A note attribute.** Type 2 (profile-specific) gets its meaning from a
  MIDI-CI Profile enabled on the channel, and no published Profile defines
  voice assignment, so this would mean inventing one and negotiating it.
  Type 1 (manufacturer-specific) carries the same 16 bits as a private
  convention, with no negotiation. Either way, a note carries only one
  attribute, and KeyEDO's notes already use it for Pitch 7.9. Pitch would
  then come from Pitch 7.25 sent just before the note-on.
- **An assignable per-note controller**, sent at the same timestamp just
  before the note-on. Pitch stays in 7.9, the voice number can be 32 bits,
  and a synth that ignores the controller still plays in tune. **Leaning
  this way.**

What it asks of Umpire: the clip model keeps the whole note-on, attribute
and per-note controllers included, so the controller's choices record and
play back unchanged (milestone 6's room for per-note data). The piano roll
shows a note's voice as a property of the note (lanes or colours), so it can
be seen and edited. For CLAP plugins, uw-core passes UMP through untouched to
plugins that accept MIDI 2.0. For other CLAP plugins the host could later
turn it into CLAP's own terms: a `note_id` on the note-on, and a per-note
parameter value (`CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID`) on that `note_id`.
CLAP's `note_id` also lets two notes share a key and channel, for plugins
that declare `CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES`. The host can't
choose a plugin's voice slot; voice allocation stays the plugin's. The synth
side is in plugins-roadmap.md (Tier 3).

## MIDI 2.0-specific features

Possible features that MIDI 2.0 makes available to Umpire, by layer of the
specification. Pitch 7.9 is covered in idea 1. ★ marks the ones a DAW
designed around MIDI 1.0 would find hardest to add later.

### Channel voice messages

1. **16-bit velocity.** Real precision for velocity editing and curves.
2. **32-bit controllers, pitch bend and channel pressure.** Smooth automation
   with no steps, and no 14-bit MSB/LSB pairs to manage.
3. ★ **Single-message RPNs and NRPNs** (16,384 registered, 16,384
   assignable). MIDI 1.0's four-message sequences depended on remembered
   state. Each MIDI 2.0 event stands alone, so cut, copy and seek can't leave
   a controller half-set.
4. **Relative controllers.** Increment and decrement versions of registered
   and assignable controllers, for encoders. A recorded nudge stays a nudge.
5. **Bank and program in one message.** Atomic patch changes, shown as one
   event ("Strings 3 / Bank B").
6. ★ **Per-note pitch bend on one channel.** Per-note glides without MPE's
   channel-per-note scheme. Notes stay on their track's channel and the bend
   belongs to the note.
7. ★ **Per-note controllers** (256 registered, 256 assignable, per note).
   Each note can have its own expression lanes.
8. ★ **Per-note management.** Detach a note's controllers (a release tail
   keeps its expression while a new note on the same key starts fresh) or
   reset them. Retrigger and legato become explicit and editable.
9. ★ **Articulation through note attributes.** Manufacturer- or
   profile-specific attributes let each note carry its articulation instead
   of keyswitch notes. The Orchestral Articulation Profile builds on this
   (check how far it has been adopted).
   A note attribute could also name a synth voice; see
   [Voice assignment by the controller](#voice-assignment-by-the-controller).

### UMP stream and structure

10. **Groups: 256 channels per connection.** A whole orchestral template over
    one device connection.
11. ★ **Endpoint discovery and Function Blocks.** Devices report named blocks,
    group ranges and direction. Tracks and routing can be created from that,
    and updated when a device changes its function blocks while running.
12. ★ **Product Instance ID.** A stable identity for one physical unit.
    Routing is remembered per device rather than per port name, so a renamed
    port or a different USB socket doesn't break a project.
13. **Stream Configuration.** Umpire can switch an endpoint to the MIDI 2.0
    protocol and turn on JR timestamps.
14. **JR timestamps.** Record when a note was played, not when it arrived.
    Umpire can also send them to devices.
15. ★ **Flex Data**: tempo, time and key signature, chord names, metronome
    settings, lyrics and text, as standard messages. Possible features:
    - a chord track that arrangers and accompaniment devices understand,
    - a lyrics track sent to devices that display text,
    - click settings (beats, accents, subdivisions) sent to hardware,
    - key and tempo travelling with exported clips.
16. **Start/End of Clip and Delta Clockstamps.** Clips are self-contained on
    disk (MIDI Clip File) and on the wire.
17. **8-bit SysEx and Mixed Data Sets.** Faster, 8-bit-clean sample, preset and
    firmware dumps, without 7-bit packing. Room for a built-in librarian.

### MIDI-CI

18. ★ **Profiles** (Default Control Change Mapping, Drawbar Organ, Rotary
    Speaker, MPE, Orchestral Articulation). Detect them, switch them on, and
    set up the matching editor and controller mapping. MPE on input only
    (decisions.md, "MPE").
19. ★ **Property Exchange** (named device data, as JSON):
    - **total recall for hardware synths**: device state stored in the
      project and restored when it opens,
    - a patch browser built from the device's own program list,
    - automation lanes named with the device's controller names and ranges
      instead of "CC 74",
    - channel and device information in the routing screen.
20. ★ **Process Inquiry.** Ask a device for its current controller values:
    check state when a project opens, check after a seek that the device
    matches what playback expects, record a starting state with a
    performance.
21. **Two-way communication as standard.** Value feedback for motorized
    faders and encoder rings, without vendor-specific SysEx.

### Transport

22. **USB MIDI 2.0 and Network MIDI 2.0.** More bandwidth, so dense per-note
    data is practical. Network MIDI 2.0 (UDP with loss recovery) allows
    multi-machine rigs and remote devices.
23. **Translation per device.** Record and store MIDI 2.0. For MIDI 1.0
    devices, scale values down and turn per-note data into channel messages
    at output, so one project plays on old and new gear. Never MPE
    (decisions.md, "MPE"); MPE is converted to MIDI 2.0 on input only.

### Strongest bets

3 and 6–9 (simpler, more exact data than MIDI 1.0 DAWs), 11–12 (routing that
sets itself up and survives reconnecting), 15 (standard chord, lyrics and
metronome tracks) and 19–20 (hardware total recall and state checks).
Support is uneven, especially for MIDI-CI, so each needs a graceful fallback
when a device doesn't answer.

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
