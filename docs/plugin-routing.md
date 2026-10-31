# Plugin routing: the graph, its mixers, and its views

**A proposal, 2026-10-30, on the Linux PC. Not decided yet:** the author
reviews the mockups first (below). It follows
[ui-direction.md](ui-direction.md), which already decided that "the model
is a graph; rows are its default view", and fills in what that graph holds
once a track has more than one plugin.

## Why now

Milestone 8 made a track's output either a MIDI device or one CLAP
instrument. Playing it on Linux (2026-10-30) raised what that leaves out:

- **Effects on either side of the instrument.** MIDI (note) effects before
  it, an arpeggiator or a scale quantizer; audio effects after it. The
  same for an external synth: its MIDI effects run in Umpire, and its audio
  comes back later (milestone 18).
- **Sidechains, audio and MIDI.** A compressor keyed from the drums is
  common. CLAP note effects can take a second note input too: a harmoniser
  driven by a chord track. Either one makes a track's routing depend on
  another track's.
- **Combining.** Two tracks into one synth; live playing and clip playback
  into one track (which `Player` already merges, silently: below).
- **StageHand's routing is too limited** and needs the same rework, so the
  model and engine belong in uw-core, built once (decisions.md, "Shared
  code").

## The model

The session is a graph of **nodes** joined by **cables**.

- **Nodes:** a track (its clips and the live input, the source of its
  notes), a CLAP plugin (note effect, instrument or audio effect), a MIDI
  device output, a MIDI mixer, an audio mixer, and the session's audio out.
- **Ports are typed and named:** note or audio, in or out, and main or not.
  A plugin's ports are the ones it declares (CLAP's note-ports and
  audio-ports extensions); a sidechain is simply an input port that isn't
  the main one.
- **An input takes one cable. An output may feed any number.** Copying a
  signal to several places needs no decision; combining several does, so
  it always goes through a mixer node, whose settings say how. This is
  Eurorack's and VCV Rack's rule, not Max's and Pd's (an input that sums
  whatever reaches it). No input ever sums or merges silently.
- **No loops.** A cable that would make one is refused when it's drawn,
  with the reason; MIDI has real loops (thru, virtual ports), so the
  engine guards against them too (ui-direction.md, "The spec's two
  features").
- **Every edit is a command**, undone like everything else; the graph is
  saved with the session (milestone 9).

### The MIDI mixer

N note inputs, one note output. Per input: on or off, and optionally a
channel or group to move it to (often the real answer for MPE: separate
the sources so they never collide). For the whole node, what to do when a
key is already sounding from another input:

| Policy | A second note-on for a sounding key | Its note-off |
|---|---|---|
| **Merge** (default) | swallowed; counted | sent only when the last source releases |
| **Retrigger** | sent | each sent, but the key stays on until the last |
| **First wins** | dropped | dropped |
| **Louder wins** | replaces the note if its velocity is higher | follows the note that sounds |

Per-note expression (MIDI 2.0 per-note controllers and pitch, CLAP's
note expressions) follows whichever input owns the sounding note; from the
other input it's dropped. A note-off always reaches the node that got its
note-on, even after a cable or a setting changes (as `Player::thru` does
today with `thruHeld`).

### The audio mixer

N audio inputs, one output: gain and pan per input, mute, and a level
meter. It's the same node as milestone 14's mixer strip, so the console
and an inline mixer in a chain are one component.

### Every track starts with a MIDI mixer

A track's notes come from two places: its clips and the live input (what
you play while it's touched). `Player` merges them now, with its own
built-in rule. In this model that becomes a MIDI mixer at the head of
every track, with two inputs, Clips and Live, and Merge as the default.
It's normally invisible: its settings show only when they aren't the
default ("Hide controls, never hide state": a badge if they differ).

## The views

All three edit the same graph; none is a separate mode.

### 1. The stack, in the track's header

The output control on a track's row becomes its **stack**.

- **Collapsed** (the default), one line: the destination (a plugin or a
  device), red with "not connected" when it's missing, E for a plugin's
  editor, and a count of what else is in the chain: "Surge XT +2", so a
  hidden arpeggiator is never invisible state.
- **Expanded**, the track's row grows to fit and its lane grows with it;
  collapsing shrinks it back. Three sections, top to bottom:
  - **MIDI**: note effects, in order.
  - **Destination**: the instrument or device. Clicking it opens the same
    chooser the output combo has now.
  - **Audio**: audio effects, in order (none for a MIDI device until its
    audio comes back).
  - Each slot: name, E, bypass, remove, and drag to reorder. A **+**
    between slots adds a plugin there; the chooser offers only what fits
    (note effects in MIDI, audio effects in Audio).
  - A slot with a second input shows a picker for it, inside the slot:
    "Sidechain: Drums", "Note input 2: Chords". This is how sidechains
    appear in the stack.
  - A mixer in the chain is a slot like any other, showing its inputs and
    policy when expanded.
- **When it applies:** a track whose routing is a straight chain, plus
  side inputs and mixers, shows as a stack. That's most tracks.
- **When it doesn't:** routing with branches (a split into two parallel
  chains, a multi-output instrument going two places, one effect feeding
  two tracks) can't be drawn as a line. Then the collapsed line shows the
  destination and a **patched** badge, and expanding it opens the patch
  view instead of the stack.

### 2. The patch view, a popover

A modular-style view, like PolygraphModularUnified's: nodes as boxes with
their ports, cables between them, opened from a track's stack ("Patch...")
or from the palette. It starts on the touched track's neighbourhood (what
feeds it and what it feeds, one step out), so it stays readable; panning
reaches the rest. Anything is possible here, and the stack keeps showing
what it can.

- Dropping a second cable on a taken input inserts a mixer node there,
  wired to both, and selects it: the combining is explicit and editable,
  but it takes one gesture. The stack's **+** does the same when it adds a
  sidechain source that's already used.
- Later this grows into the full routing view (ui-direction.md, "The
  spec's two features": 2D first, by stage, with loop and orphan checks).

### 3. Mixer strips

Milestone 14's console: a strip per track and per bus. The audio strips
are the audio mixer node's controls. MIDI strips are the same idea for
notes, per track: the head mixer's policy, channel or group, transpose,
velocity scaling, and where the notes go. DAWs bury these in each track's
settings; with UMP groups as the routing backbone (ideas.md, 5) they may
be where Umpire stands out. Following "empty means invisible", no mixer
shows until there's something to mix.

## The engine

- **A buffer per node.** Each node renders into its own buffers; mixers
  read their inputs' buffers. Nothing renders straight into the output
  any more (milestone 8's `AudioEngine::renderBlock` does now: every voice,
  in map order, summed).
- **Dependency order.** Nodes run in an order worked out from the cables,
  recomputed on the main thread when the graph changes and handed to the
  audio thread whole, never edited in place. A sidechain source runs
  before what it keys.
- **Notes inside a block.** A note effect's output events, at their sample
  offsets, are the next node's input events in the same block, so a chain
  of note effects adds no latency.
- **MIDI device destinations.** Their note effects run in the engine too,
  so a device chain needs the audio thread running even with no plugin
  audio. What comes out is scheduled to the device as now (milestone 7's
  sendAt), at the block's time plus the output latency. How this lines up
  with ALSA's queue is open (below).
- **Latency.** Plugins report latency; delay compensation across parallel
  paths comes later (with milestone 18's), but the graph keeps each
  node's latency from the start.

## Shared with StageHand: uw-core

The graph, the mixers and the scheduler are built in uw-core, headless
and tested there, then used by both apps; StageHand's rack moves onto them
afterwards. Umpire's views (stack, patch, strips) stay Umpire's.

## Steps, if this is accepted

Each about a milestone. They'd come after 9 and 10 unless the author
reorders:

1. **The graph (uw-core):** nodes, typed ports, cables, the one-cable
   rule, loop refusal, commands, dependency order. Headless tests.
2. **The engine on the graph (uw-core, Umpire):** buffers per node, the
   order on the audio thread, events through note effects. Today's
   routing (track to one destination) rebuilt on it, sounding the same.
3. **The MIDI mixer, and the track head:** `Player`'s silent merge
   replaced by the head mixer, with its policies tested.
4. **The stack:** collapsed and expanded, note effects and audio effects,
   the row and lane growing.
5. **Side inputs and the audio mixer:** sidechain pickers in slots, mixer
   slots, the inserted-mixer gesture.
6. **The patch view.**

Mixer strips stay with milestone 14.

## The mockups

On a claude.ai canvas, "Umpire: Plugin Routing"
(https://claude.ai/artifact/VEpCTv3BTQD17NeYMDoyAw), private to the
author. Described here, since sessions on other machines can't open it:

1. **Collapsed.** Four tracks. Keys: "Surge XT +2". Bass: an external
   synth, "Sub 37 +1" (an arpeggiator before it). Drums: "StruckKit",
   patched (a multi-out kit to two places). Pad: "Vital (not connected)" in
   red.
2. **Keys expanded.** The row grows: MIDI (Arpeggiator, Scale quantize),
   Surge XT with E, Audio (Compressor with "Sidechain: Drums", Reverb), +
   between slots. The lane is as tall as the row.
3. **A MIDI mixer in a stack.** Lead's chain starts with a MIDI mixer
   (Lead's own notes and the Chords track), expanded to show its two
   inputs and the policy, Merge.
4. **The patch view.** A popover over the dimmed track view, centred on
   Keys: its head mixer (Clips, Live; Merge), Arpeggiator, Scale
   quantize, Surge XT, Compressor, Reverb. Drums' StruckKit has two
   outputs: 1-2 fans out to the compressor's sidechain and to an audio
   mixer ("Out mix"), 3-4 goes to the same mixer, and Keys' reverb is its
   third input; the mixer feeds the session's audio out. Notes are teal
   cables, audio grey; a footer says "One cable per input; outputs fan
   out".
5. **Mixer strips.** MIDI strips (same-key policy, channel or group,
   transpose, velocity, note effects or inputs, destination; values that
   aren't the default outlined in amber) beside audio strips (pan, fader,
   meter, M and S, and the audio out).

## Open

- Does a track's head mixer show in the stack at all, or only as a badge
  when it isn't the default?
- Bypass vs remove: is bypass worth a control per slot?
- What "patched" routing the stack might still draw (a two-way split as two
  short columns?) before giving up.
- How note effects on a MIDI device's chain line up with ALSA's scheduling
  (the engine's block clock against the queue's lookahead).
- The same plugin in several places: separate instances always, or a way
  to share one (a send)?
- Buses and sends: an audio mixer with its own strip, or a separate node?
- Whether a clip's own instrument (ui-direction.md) is a second
  destination in the stack, or a switch in front of it.
