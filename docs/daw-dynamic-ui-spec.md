# DAW Dynamic UI: Design Spec for Two Features

Hand-off document for Claude Code. It describes two interactive UI concepts that were prototyped as throwaway widgets in a chat. The prototypes were HTML and canvas, rendered inline. They are **behavioral references, not source to port**. Re-implement the behavior in whatever UI stack the DAW project uses.

---

## 0. Context and principles

The original idea: most DAW UIs are static, because one layout has to serve every task. This spec explores **intent-shaped, non-linear surfaces** that spawn on top of a stable core. The two features below are the ones chosen for mockups.

Principles that should constrain every implementation decision:

1. **No AI dependency.** Both features are ordinary, deterministic DAW features. They work with no network and no model. An AI agent is an optional authoring layer on top (see section 3.8) and must never be required.
2. **Additive, not replacing.** The timeline, transport, and mixer stay stable (engineers rely on muscle memory). These surfaces are overlays or panels the user opens deliberately.
3. **Changes happen because the user asked.** Never rearrange or modify the UI or the project on a guess.
4. **Everything visible is backed by editable structured data.** Feature 1 is a view of the routing graph. Feature 2 is a view of a mapping table. The user can always see and edit what is driving what.
5. **Reversible.** Every action the surface performs must go through the host's normal undo system.
6. **Theme-aware.** Prototype colors were hard-coded hex for node types; a real build should pull neutrals from the host theme and keep only the semantic hues below.

Semantic colors used in both prototypes:

| Meaning | Hex |
|---|---|
| Track / neutral | `#888780` |
| Bus | `#7F77DD` |
| Return / aux | `#D85A30` |
| Master | `#1D9E75` |
| Highlight / active path / accent | `#378ADD` |
| Problem / error | `#E24B4A` |

---

## Feature 1: 3D signal-flow (routing) view

### 1.1 Purpose

Make routing visible. Depth (the Z axis) encodes **signal stage**: tracks at the front, then buses and returns, then master at the back. Sends, bypasses, feedback loops, and orphaned tracks become visible at a glance, which a flat mixer or routing matrix does poorly.

### 1.2 Data model

Treat the session as a directed graph.

```ts
type NodeType = 'track' | 'bus' | 'return' | 'master';
type EdgeKind = 'out' | 'send';   // 'out' = main output routing, 'send' = aux/parallel send

interface RouteNode {
  id: string;
  label: string;
  type: NodeType;
  depth: number;      // stage index, see 1.3
  x: number;          // horizontal layout position (see 1.3)
}

interface RouteEdge {
  from: string;       // node id
  to: string;         // node id
  kind: EdgeKind;
}
```

The graph should be **derived from the live project** (track outputs, bus assignments, send slots), not stored separately. Edits made in the view (for example "fix" actions) write back to the project through its normal routing API and undo stack.

### 1.3 Layout

Prototype sample layout (hard-coded; a real build computes it):

- Tracks (depth 0): Kick, Snare, Bass, Synth, Vox, Perc at x = -250, -160, -50, 40, 140, 240
- Buses and returns (depth 1): Drums bus (x -205), Music bus (x -5), Reverb return (x 195)
- Master (depth 2): x = 0

**Real-build rule for depth:** compute each node's depth as the longest path from any source node, ignoring back-edges that close cycles (find them with a DFS). Nodes in a cycle should still get a sensible depth, so use the DFS-order result rather than failing.

**Placement (world space, before rotation):**

```
worldX = node.x * 0.95
worldY = 70 - depth * 70                    // later stages sit higher
worldZ = (centerDepth - depth) * zGap * spread
         // prototype: centerDepth = 1, zGap = 170, so z = (1 - depth) * 170 * spread
```

`spread` is a user control from 0 to 1. At 0 the view collapses to a flat layout, which doubles as a built-in comparison with the traditional mixer.

For x positions in a real session, order nodes within a stage to minimize edge crossings (a barycenter heuristic is enough) and space them evenly.

### 1.4 Projection and rendering

Camera is a simple orbit with perspective. Reference math from the prototype:

```
cy = cos(yaw); sy = sin(yaw); cp = cos(pitch); sp = sin(pitch)
x1 = x*cy + z*sy
z1 = -x*sy + z*cy
y2 = y*cp - z1*sp
z2 = y*sp + z1*cp
s  = 800 / (800 - z2)                // perspective scale; larger z2 = nearer
k  = min(1, canvasWidth / 640)       // responsive scale
screenX = W/2 + x1 * s * k
screenY = H/2 + y2 * s * k + 10
```

- Initial view: `yaw = -0.55`, `pitch = 0.32`. Clamp `yaw` to ±1.3 and `pitch` to [-0.1, 0.85].
- Drag sensitivity: `yaw += dx * 0.008`, `pitch += dy * 0.006`.
- **Nodes are billboards** (cards that always face the camera), about `86 × 36` px times the perspective scale `s`. Text is `500 13px * s`, with a minimum of 11 px. Each card has a colored border by node type and a thin colored bar along the bottom.
- **Draw order:** all edges first, then nodes sorted far to near (by `z2` ascending).
- **Edges:** straight lines from card edge to card edge, with an open-chevron arrowhead near the destination. `out` edges are solid, `send` edges are dashed (`[5,4]`). Base width about 1.4 px, scaled by destination perspective.
- **Flow animation:** a small dot travels along each active edge, direction from source to destination. Phase = `((timeMs / 1800) + edgeIndex * 0.137) % 1`. Disable when the user prefers reduced motion.
- Support `devicePixelRatio` for crisp rendering.

In a real DAW, prefer GPU rendering (instanced quads for nodes, line batches for edges) so a session with 80+ tracks stays smooth.

### 1.5 Interactions

| Input | Behavior |
|---|---|
| Drag on empty space or nodes | Orbit camera |
| Click a node (pointer moved < 5 px) | **Trace**: select it. Click it again, or click empty space, to clear. |
| Depth slider (0-100%) | Sets `spread` |
| "Show sends" toggle | Show or hide `send` edges |
| "Flag problems" toggle | Enable problem highlighting and the problems panel |
| Reset | Restore default camera, default depth, and the original routing |

**Trace behavior.** For a selected node, compute `upstream` (everything that feeds it, reverse reachability) and `downstream` (everything it flows to, forward reachability).

- Nodes in `upstream ∪ downstream ∪ {selected}` stay at full opacity. All others fade to about 0.28.
- Edges whose endpoints both lie in the upstream set (plus the selected node), or both lie in the downstream set (plus the selected node), are highlighted (`#378ADD`, width about 2). Other edges fade to about 0.14.
- Info line text: `"<Label> — fed by: <list or 'nothing'>. Flows to: <list or 'nowhere'>."`
- Default info line (nothing selected): `"Drag to orbit. Click a node to trace its path. Turn on Flag problems to find loops and orphans."`

### 1.6 Problem detection (plain graph analysis)

Both checks are deterministic graph analysis and need no AI.

- **Feedback loop:** an edge `(u → v)` is in a cycle if `v` can reach `u` (or `u == v`). Mark all such edges and their endpoints. Render cycle edges red (`#E24B4A`, wider) and give cycle nodes a red border. Panel text: `"Feedback loop: A ↔ B."`
- **Orphan:** any node that is not the master and has no outgoing edge. Give it a red border and a small red "no output" label under the card. Panel text: `"Orphaned: <names> goes nowhere."`
- If neither is found: `"No routing problems found."`

**Fix actions** (shown as buttons next to the problem text; each executes through the project's routing API and undo):

- Orphan: route the orphan to a chosen target (prototype: "Route Perc to Drums bus"). A real build should let the user pick the target.
- Feedback loop: re-route the offending output (prototype: "Send reverb to master instead", which removes `rev → drums` and adds `rev → master`). A real build should propose the loop-closing edge as the default choice and let the user choose.

**Note on loops:** some feedback is intentional in some workflows. The view should flag, not forbid. Offer a way to mark a loop as intentional so it stops being flagged.

### 1.7 Sample data (for tests and the demo)

```ts
const nodes = [
  {id:'kick',  label:'Kick',      type:'track',  depth:0, x:-250},
  {id:'snare', label:'Snare',     type:'track',  depth:0, x:-160},
  {id:'bass',  label:'Bass',      type:'track',  depth:0, x:-50},
  {id:'synth', label:'Synth',     type:'track',  depth:0, x:40},
  {id:'vox',   label:'Vox',       type:'track',  depth:0, x:140},
  {id:'perc',  label:'Perc',      type:'track',  depth:0, x:240},
  {id:'drums', label:'Drums bus', type:'bus',    depth:1, x:-205},
  {id:'music', label:'Music bus', type:'bus',    depth:1, x:-5},
  {id:'rev',   label:'Reverb',    type:'return', depth:1, x:195},
  {id:'master',label:'Master',    type:'master', depth:2, x:0},
];
const edges = [
  ['kick','drums','out'], ['snare','drums','out'],
  ['bass','music','out'], ['synth','music','out'],
  ['vox','master','out'],                 // bypasses the buses, visible in 3D
  ['drums','master','out'], ['music','master','out'],
  ['snare','rev','send'], ['vox','rev','send'], ['drums','rev','send'],
  ['rev','drums','out'],                  // forms a feedback loop with drums→rev
];
// 'perc' has no outgoing edge and is an orphan.
```

Expected results: the cycle is `{drums, rev}`, and the orphan list is `[perc]`.

### 1.8 Real-build considerations

- **Routing types:** distinguish audio, MIDI, and sidechain routing. Provide a filter or layer toggle per type.
- **Scale:** collapse groups or folders into single nodes that expand on click. Use level-of-detail so distant cards become simple glyphs.
- **Meters on nodes:** show live level on each card so you can see where signal is hot.
- **Drag-to-patch:** drag from one node to another to create a route or send (through the routing API and undo).
- **Latency view:** optionally encode plugin delay or PDC as edge length or a badge.
- **Accessibility:** keyboard orbit and node traversal, a text-list alternative (the same data as an outline), and respect reduced motion.
- **Selection sync:** selecting a node here should select the matching track or bus in the main mixer, and vice versa.

---

## Feature 2: Bespoke XY controller (words mapped to parameters)

### 2.1 Purpose

A 2D pad where each axis is a **word pair** ("Boxy ↔ Airy", "Cold ↔ Warm") backed by an **editable mapping** to real parameters. It acts as a custom macro controller, and the user can always see which parameters it is moving.

### 2.2 Data model

```ts
interface ParamDef {
  id: string;
  name: string;      // e.g. 'Air shelf 12 kHz'
  unit: string;      // ' dB', '%', etc.
  min: number;
  max: number;
  base: number;      // starting (neutral) value
  decimals: number;
}

interface AxisDef {
  lo: string;        // word at the low end (left / bottom)
  hi: string;        // word at the high end (right / top)
  maps: Record<string, number>;   // paramId → signed amount, -100..100 (percent of half-range)
}

interface Controller {
  name: string;
  x: AxisDef;        // horizontal axis
  y: AxisDef;        // vertical axis
}
```

**Value formula** (the core of the feature). With puck position `x, y ∈ [-1, 1]` (center = 0, **y positive is up**):

```
half  = (param.max - param.min) / 2
delta = (xAmount * x + yAmount * y) / 100          // amounts default to 0 if unmapped
value = clamp(param.base + delta * half, param.min, param.max)
```

So an amount of +100 on an axis pushes the parameter by its full half-range at the axis extreme. Negative amounts invert the direction. Contributions from the two axes add.

### 2.3 Prototype parameter set

The prototype used a generic channel strip:

| id | name | unit | min | max | base | decimals |
|---|---|---|---|---|---|---|
| `low` | Low shelf 100 Hz | dB | -12 | 12 | 0 | 1 |
| `mud` | Cut 300 Hz | dB | -12 | 12 | 0 | 1 |
| `pres` | Presence 3 kHz | dB | -12 | 12 | 0 | 1 |
| `air` | Air shelf 12 kHz | dB | -12 | 12 | 0 | 1 |
| `drive` | Saturation | % | 0 | 100 | 20 | 0 |
| `rev` | Reverb mix | % | 0 | 60 | 10 | 0 |
| `width` | Stereo width | % | 0 | 200 | 100 | 0 |

In a real build, `ParamDef` is generated from **any automatable parameter** on any track, device, or plugin. A single axis may mix parameters from different plugins.

### 2.4 Prototype presets

```ts
const presets = {
  vocal: {   // "Vocal tone"
    x: { lo:'Boxy',  hi:'Airy',   maps:{ mud:-70, air:80, pres:40 } },
    y: { lo:'Cold',  hi:'Warm',   maps:{ low:50, drive:40, air:-20, pres:-25 } },
  },
  synth: {   // "Synth space"
    x: { lo:'Dry',   hi:'Wet',    maps:{ rev:90, width:50 } },
    y: { lo:'Dark',  hi:'Bright', maps:{ air:70, pres:30, low:-25 } },
  },
  drums: {   // "Drum punch"
    x: { lo:'Soft',  hi:'Hard',   maps:{ drive:80, pres:35, mud:-30 } },
    y: { lo:'Tight', hi:'Roomy',  maps:{ rev:70, width:25, low:20 } },
  },
};
```

### 2.5 UI layout and behavior

**Pad**

- Square canvas. The top word is above it, the bottom word is below it (center), the left word is below it on the left, and the right word is below it on the right. The prototype prefixed them with arrows (`←`, `→`, `↑`, `↓`).
- Pointer down or drag sets the puck position, clamped to the pad. Keyboard: arrow keys move the puck by 0.05 per press (the pad must be focusable).
- Draw a center crosshair (hairline), a crosshair through the puck, and a filled puck (radius about 11 px, accent color, with a ring in the surface color).
- **Shade-by-parameter:** a dropdown ("Shade pad by") picks one parameter, or "Nothing". The pad is divided into a 20×20 grid. For each cell, compute the parameter's value at that cell's `(x, y)`, normalize to `t = (value - min) / (max - min)`, and fill with the accent color at alpha `t * 0.4`. This shows exactly where on the pad each parameter is high or low.
- **Pins:** "Pin spot" stores the current `(x, y)` (maximum 8). Pins draw as numbered diamonds. Numbered buttons recall a pin. A trash button clears all pins. Pins are snapshots in "feel space" rather than parameter space.

**Axis editors** (one block per axis)

- Two text inputs edit the axis words live, and the pad labels update as the user types.
- A list of mapping rows, each with: parameter name, a slider (-100 to +100, step 5), a signed percentage readout (`+50%`), and a remove button.
- An "Add parameter…" dropdown lists parameters not yet mapped on that axis. Adding inserts the mapping at +50.

**Live meters** (below the pad)

- One meter per parameter, showing name and current value (signed, with unit, for dB values).
- A tick marks the base value. The fill spans from the base to the current value, so direction and size read at a glance.
- Parameters that neither axis maps are dimmed (opacity about 0.45).

**Controller switcher**

- A "Controller" dropdown loads a preset. Loading resets the puck to center, clears pins, and sets the shade parameter to the first parameter mapped on the X axis.

### 2.6 Real-build considerations

- **Host binding:** moving the puck writes real parameter values (and records automation when the transport is armed). The meters show the real, current values from the host.
- **Undo and safety:** group puck gestures into single undo steps. Provide "return to starting values." Respect per-parameter limits (for example, stop at a safe max for a saturation drive or a feedback amount).
- **Smoothing:** apply parameter smoothing to avoid zipper noise on fast gestures.
- **Nonlinear mappings:** the prototype is linear only. Add per-mapping response curves, because dB, Hz, and ratio do not feel linear. Hz especially should map logarithmically.
- **Mapping persistence:** save controllers as small preset files (see 2.7), shareable between users and projects.
- **More dimensions:** later, a third axis could come from an expression input (mod wheel, pressure), or several pads could be layered.
- **Other bindings:** allow MIDI or hardware surface control of the puck (MIDI learn on X and Y).
- **Accessibility:** keyboard control (above), plus a numeric-entry fallback for X and Y.

### 2.7 Suggested file format for saved controllers

```json
{
  "version": 1,
  "name": "Vocal tone",
  "x": {
    "lo": "Boxy",
    "hi": "Airy",
    "maps": [
      { "param": "<host parameter id>", "amount": -70, "curve": "linear" },
      { "param": "<host parameter id>", "amount": 80,  "curve": "linear" }
    ]
  },
  "y": {
    "lo": "Cold",
    "hi": "Warm",
    "maps": [
      { "param": "<host parameter id>", "amount": 50, "curve": "linear" }
    ]
  },
  "pins": [ { "x": 0.25, "y": -0.4 } ]
}
```

`curve` is reserved for the nonlinear extension (`"linear"` is the only value the prototype implemented). Parameter references should be stable host IDs, and loading must degrade gracefully (skip and report) when a referenced parameter no longer exists.

### 2.8 Optional AI authoring layer

This is the only AI-dependent part, and it is strictly optional. The prototype had a text field ("Describe a sound…") and an "Ask Claude" button that sent a prompt to the chat. The prompt template was:

```
Propose XY pad axes for this sound description: "<user text>".
Use only these parameters: <comma-separated parameter names>.
Give each axis a word pair and signed percentage amounts per parameter.
```

Empty input shows an inline validation error ("Describe a sound first") and sends nothing.

A real integration should:

- Request **structured output** matching the `Controller` shape in 2.2, validated against the available parameter IDs.
- Load the result into the **normal editor** as an ordinary controller, never apply it silently. The user can tweak or discard it exactly as if they had built it by hand.
- Clamp amounts to -100..100, drop unknown parameters, and log what was dropped.
- Be fully hidden or disabled when no model is configured. Nothing else in the feature may depend on it.

---

## 3. Shared guidance

### 3.1 Architecture suggestion

Keep each feature as a **view over a model**: the routing graph (Feature 1) and the controller mapping (Feature 2) are data. Rendering, input handling, and host integration are separate layers. This keeps the AI layer, presets, and tests all operating on the same plain data.

### 3.2 Testing

- **Feature 1:** unit-test cycle detection, orphan detection, and reachability (trace) against the sample data in 1.7. Test depth layout on graphs that contain cycles. Test that fix actions produce the expected edge changes and are undoable.
- **Feature 2:** unit-test the value formula (center → all values equal `base`; extremes with amount ±100 reach `base ± half`; clamping at `min`/`max`; two-axis addition; unmapped parameters unchanged). Test preset load and save round trips, and graceful handling of missing parameters.
- **UI:** keyboard operation for both views, reduced-motion behavior, and a light/dark theme check.

### 3.3 Acceptance criteria

Feature 1:
- [ ] Opening the view shows the live routing graph with depth by stage, with orbit and depth controls.
- [ ] Clicking a node isolates its upstream and downstream paths and shows the info line.
- [ ] "Flag problems" correctly finds cycles and orphans on the sample session, and the fix buttons modify real routing with undo.
- [ ] At depth 0% the layout is flat and still readable.
- [ ] Works with no network or AI.

Feature 2:
- [ ] The pad moves real parameters per the value formula, and the meters reflect actual host values.
- [ ] Axis words and mappings are editable live, and the pad shading shows any chosen parameter.
- [ ] Pins save and recall positions. Controllers save and load, and missing parameters are handled gracefully.
- [ ] The optional AI field is absent or disabled when no model is configured, and everything else still works.

### 3.4 Backlog (ideas discussed but not mocked up)

- **A/B/C mix layers:** versions of a mix stacked like transparent sheets that can be peeled back, with differences highlighted.
- **Frequency as a third axis:** tracks on the timeline with a spectral depth layer, making masking visible.
- **Task-shaped temporary workspaces:** for example, "why is my low end muddy?" opens a transient panel with only the relevant tracks, a collision overlay, and the controls that matter. These dissolve when dismissed. They could be built deterministically from analysis tools, with the AI only as an optional way to request them.
- **Context-sensitive plugin front panels:** a simplified consistent layer over plugin UIs, with the full UI one click deeper.
- **Action log anchored to the surface:** gestures on generated surfaces logged as human-readable, reversible entries (for example, "cut 3 dB at 250 Hz on Bass").

### 3.5 Open questions for the project

- Which UI/rendering stack does the DAW use, and does it have a hardware-accelerated 3D or canvas surface available?
- Does the host expose a routing graph API (read, write, undo), and a uniform parameter-enumeration API for plugins?
- Where should these surfaces live: dockable panels, floating windows, or overlays on the mixer?
- Are there any host constraints on feedback loops (many DAWs forbid or auto-compensate them), which would change how the loop check should behave?
