# The UI direction: only what you need

Decided 2026-10-23 on the Mac, after reading
[daw-dynamic-ui-spec.md](daw-dynamic-ui-spec.md) and looking at mockups. The
decisions are summarized in decisions.md ("The UI direction"); this file
says what they mean and describes the mockups, since the mockups live in a
private claude.ai canvas ("Umpire: Only What You Need",
https://claude.ai/artifact/BpJESujowUiMnws4KSB7Ms) that sessions on other
machines can't open.

## The principle

The opposite of Bitwig: you see only what you need to see. The screen grows
with the music, not with the feature list. The timeline is the one surface
that is always there; MIDI and audio are time-based, so time stays the main
axis. Everything else appears because there is content, or because the user
touched or asked for it.

Two rules keep that from going wrong:
- **Hide controls, never hide state.** Anything that changes what you hear
  (mute, a transpose on a clip reference, a split, a clip's own instrument, a
  loop in the routing, a patch Umpire can't recall) shows at a glance, at
  least as a badge. Minimal UIs fail when something invisible changes the
  result.
- **Nothing changes on a guess.** Things appear because content exists or
  the user asked, never because Umpire predicted it. When something appears,
  it appears in the same place every time.

## How things appear

- **Empty means invisible.** No pitch-bend lane without pitch bend, no
  per-note expression on notes without it, no mixer until there's something
  to mix. A device shows only the parameters it declares (ideas.md, 2).
- **Details appear in place.** Touching a track, clip or note shows its
  controls on or beside it, not in a fixed inspector. Clicking away hides
  them.
- **Zoom replaces opening things** (ideas.md, 7): sections, bars, notes,
  per-note expression. Notes can be edited in the lane by zooming into it.
  Double-clicking a clip still opens the grid in its own window for detailed
  work (decisions.md, "The track view"): both exist.
- **The graph is seen only where you touch it.** The model is a graph (below),
  but the default view never draws all of it. A touched track shows its path
  (Keys → Surge XT). A full routing view is something the user opens.
- **Summon, don't switch.** A command palette (Cmd/Ctrl+K) finds any command
  and acts on the touched track. Some commands open a **task surface**: a
  band in place (the split, below) that goes away when done and leaves a
  badge behind.
- **The keyboard keeps it fast**, and empty states and the palette make
  hidden features discoverable.

## The model behind it

- **A track is a node with a lane.** The session is a graph: inputs,
  tracks, processors (splits, filters, transforms), instruments. Tracks are
  the nodes that hold clips; the track view draws them as rows, in an order
  the session stores. Tracks get stable IDs, and where MIDI comes from and
  goes to are edges, not fields on the track. This keeps a full graph view
  possible later without building one now.
- **Input belongs to the session, not the track.** A DAW usually has one
  hardware controller and maybe one software one (KeyEDO). Umpire listens
  to all inputs; what you play goes to the touched track, and recording goes
  to the armed tracks. A status line names what's listening. Filtering by
  channel or group (StageHand's per-row input) is the rare case: asked for
  through the palette, like a split, and shown as a badge. This replaces
  milestone 5's per-track input, channel and group choices.
- **An instrument is a plugin or a device**, and neither is the default
  assumption. External synths may be where Umpire stands out most: MIDI 2.0's
  function blocks, group names and Property Exchange matter for devices more
  than for plugins, and DAWs serve hardware users badly. A device shows the
  name it declares; its channel shows as a badge only when several tracks
  share it. Its patch isn't saved with the session unless Umpire sends a
  program change or uses Property Exchange, and the track says so.
- **A clip can choose its own instrument.** The track has a default; a clip
  reference (ideas.md, 9) may name another, as a setting on the reference
  like transpose. A track becomes a musical part, not a channel. Prior art:
  trackers, where each note names its instrument. The engine must send a
  note's note-off to the instrument that got its note-on, keep both
  instruments running across the change (tails), and decide what the new
  instrument inherits (probably nothing). Later, a track's mixer channel sums
  its instruments.
- **One undo for everything.** Every change is a command on the session, so
  the palette, task surfaces and later views all undo the same way.

## The mockups

Six static screens, 1280×760, in the provisional colours (graphite, amber
accent, teal notes, red for armed).

1. **Empty session.** A small transport (play, record, position, tempo), a
   bar ruler, and one prompt: "Play something to start", with Cmd+T (add a
   track) and Cmd+K (find anything). A status line names the inputs
   listening.
2. **Tracks, one touched.** Four tracks. A row is an activity light, a name,
   and badges only for what isn't the default: "ch 10", R (armed), M
   (muted), "+12" on a transposed clip. The touched track (Keys) opens to
   show what it plays (Surge XT), what it records, and R, M and S, and its
   lane shows its path. The status line says what's listening and that
   playing goes to the touched track.
3. **Zoomed to notes.** Zooming into Keys turns its lane into a piano roll
   in place; the other tracks shrink to thin strips. A zoom control reads
   Sections, Bars, Notes, Expression. One note with a per-note pitch glide
   shows its curve; the rest show none.
4. **Summon.** The session dims under a palette: "split keys" offers
   splitting at a note, splitting by channel, layering, and showing Keys'
   routing, acting on the touched track.
5. **A task surface: the split.** A band opens under the Keys row: a
   keyboard with a split marker at C4, below it to Keys (Surge XT), from C4
   up to Lead (Vital), and Done and Remove split. Afterwards only the badges
   remain: "split C4" on Keys, "from Keys ≥ C4" on Lead.
6. **Each clip picks its synth.** Keys plays Surge XT; clip 2 plays Vital,
   clip 4 Pianoteq. Each clip has a strip in its instrument's colour and a
   badge when it isn't the track's. The touched track lists the instruments
   it uses; the selected clip shows its path and "Use the track's synth". A
   note held from clip 1 into clip 2 is drawn dashed: still released on
   Surge XT.

## The spec's two features

[daw-dynamic-ui-spec.md](daw-dynamic-ui-spec.md) is kept as a reference.
Its principles (additive surfaces, changes only when asked, views over
editable data, everything undoable) match Umpire's. Its features:
- **The routing view** (its feature 1): build it in 2D first, as a layered
  graph with stages left to right; the 3D depth view is an experiment. It's
  worth building once routing has more than inputs → tracks → outputs: after
  instruments (milestone 8) and groups (milestone 12). Loop and orphan checks
  are plain graph code and can come with the graph model. MIDI has real
  loops too (thru loops, virtual ports), so flag them and guard the engine.
- **The word-pair XY controller** (its feature 2): targets are CLAP
  parameters (milestone 8), MIDI 2.0 controllers on outputs (milestone 7),
  or parameters a device names through Property Exchange. MIDI learn on X
  and Y, and recording the puck into clips, follow.
- Its optional AI layer is not on the roadmap.

## Open

- What badges exist, and how many a row can show before it needs a summary.
- The palette: its commands, how it names the target, its keys.
- How zooming in the lane and the grid window share the editing code.
- The full graph view: when, and in 2D or 3D.
- What a clip's own instrument inherits, and how the mixer sums a track's
  instruments (milestone 14).
- Whether StageHand-style input filtering is needed at all once splits and
  layers exist.
- Where a clip's tuning (EDO and reference) is kept, for the grid and for
  zooming into a lane (decisions.md, "Time, clips and pitch").
