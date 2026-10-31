# Stem export and the tempo map

**A proposal, 2026-10-30, on the Linux PC; not yet scheduled.** Umpire's
scope stays narrow (decisions.md, "Scope"): mastering, audio editing and
everything else Umpire leaves out happen in another DAW, working from
Umpire's stems. So getting a session out cleanly matters, and it should
be cheap to repeat: the author's experience moving Digital Performer
projects to Pro Tools was that every revision meant doing it all again by
hand.

## What's exported

**Audio only, plus the tempo map.** No MIDI notes: the stems are the
faithful version (a Standard MIDI File would drop per-note data and EDO
tuning anyway).

- **One WAV per track**, all at the same sample rate, bit depth and length,
  all starting at the session's zero (lead-in silence included) and
  running past the end for tails. Mono sources as mono files, stereo as
  interleaved. 32-bit float by default, so nothing clips; the manifest
  records each stem's peak. Broadcast WAV `bext` time stamps as well, for
  DAWs that place files by them (Logic's "move to original position").
- **The tempo map** as a tempo-only Standard MIDI File: tempo changes, time
  signatures and markers, no notes (below).
- **A REAPER project** (`.RPP`, plain text): every stem on its own named,
  coloured track at the right place, with the tempo map. REAPER runs on
  Linux and the Mac.
- **A manifest**, short and readable: each stem's track, colour, group and
  print point, the tempo, rate and start, the effects after the print
  point by name, each stem's peak, and notes the author adds.
- **A reference mix**: the whole mix in stereo, so the receiver can check
  the stems sum to it.

Not written: project files for Ardour, Audacity (`.aup3` is SQLite, but its
project encoding and audio blocks are Audacity's internal, unpublished
format, changed between 3.x releases), Logic (closed), Live (`.als` is
gzipped XML, undocumented, changes between versions) or Pro Tools (AAF:
large to write, and it carries clips, not processing). All of them import
the WAVs, and the tempo map, directly.

## The tempo map

Other DAWs need Umpire's tempo and meter to line their grid up with the
stems. The format they all import is a **Standard MIDI File** (format 1,
or format 0 with one track) holding only meta events: Set Tempo (`FF 51`),
Time Signature (`FF 58`), Key Signature (`FF 59`) where Umpire has one,
and Marker (`FF 06`). Pro Tools, Logic, Live, REAPER and Ardour read
tempo and meter from it.

- **Ramps become steps.** SMF has no tempo ramp: a gradual change is
  written as a run of small steps (every sixteenth, say). The stems are
  rendered audio, so they still line up; only the receiving grid is a
  close approximation during the ramp.
- **Precision:** SMF tempo is microseconds per quarter note, an integer,
  so fine enough; the PPQ is chosen to put every change exactly on a tick.
- **MIDI 2.0 natively is on the roadmap, not the default.** MIDI 2.0 carries
  tempo, time and key signature as Flex Data (ideas.md, 15), and a MIDI
  Clip File (M2-116-U) could hold them, but no DAW imports either yet.
  When one does, Umpire writes that too. How Umpire stores its own tempo
  map (Flex Data or its own structure) is a separate choice; export
  converts.

**First, Umpire needs a tempo map.** Today it has one tempo and only 4/4
(recording.md, "Not in it (later)"). A map (tempo changes, sudden or ramped, and
meter changes, each at a tick) turns ticks-to-seconds into a piecewise
conversion that the scheduler, recording, loop and locate, the ruler and
the render all use. It belongs before or with milestone 9: the session
file stores it, and adding it after a format exists means migrating saved
files.

## Making re-export cheap

- **Stable names**: a stem's file name comes from its track's ID and name
  (`01 Keys.wav`), so reordering tracks doesn't rename files.
- **Same folder, same settings**: "Export stems" remembers them, and
  overwrites in place. A receiving session that already uses the files
  picks up the new audio on reload, with nothing to re-import.
- **Only what changed**: milestone 15's staleness hash re-renders only
  changed tracks; a change log in the folder lists them.
- **The destination's sample rate**: 44.1 or 48 kHz, whatever the
  receiving session runs, to avoid conversion on import.

## Printing the audio effects

The routing graph (plugin-routing.md) never leaves Umpire: no interchange
format carries plugin chains, sidechains or mixers. What's chosen is where
in each track's chain the stem is captured, its **print point**:

- **Wet** (default): after everything, exactly as heard. Sidechains and
  mixer decisions are baked in. The only choice that always matches.
- **Dry**: after the instrument, before the audio effects. Redoable
  elsewhere, but a sidechained effect can't be rebuilt there.
- **A chosen slot**: print through the effects that make the sound,
  leave the mix effects (reverb, delay) to the mix engineer. Shown in the
  stack as a marker between two slots.
- **Both**, wet and dry, each named so.

MIDI effects and the instrument are always printed: notes only become
audio there. An unprinted shared effect gets its send levels in the
manifest; a sidechain's source is usually a stem already.

**Stem groups**: a track can carry a group tag (drums, bass, keys,
vocals), and the export writes one file per group, per track, or both;
this needs the routing graph's mixers.

## One render pass

All stems come from one offline pass over the whole graph, each track's
output tapped as it's rendered, not a solo pass per track: sidechains
still hear their sources, it's much faster, and plugins with randomness
agree across stems. Plugin latency is compensated in the render, so
nothing lands late. Hardware tracks can't render offline: they need
milestone 18's real-time capture, or the export skips them and says so.

## Where it fits

- **The tempo map** (model, ruler, scheduler, render): before or with
  milestone 9.
- **Stem export**: after milestone 14 (its render loop), with milestone
  15's WAV writer and staleness hash.
- **Print points in the stack and stem groups**: with plugin routing's
  stack and mixers (plugin-routing.md, R4 and R5), if accepted.

## Open

- Pre- or post-fader by default, once the mixer exists.
- The manifest's format: plain text, JSON, or both.
- Whether the REAPER project can keep a chain live (REAPER hosts CLAP, and
  its routing can express sidechains), for receivers with the same
  plugins. Needs a look at how REAPER stores CLAP state.
- How often a ramp is stepped in the SMF.
