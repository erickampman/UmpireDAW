# Plugin roadmap

Which MIDI 2.0 features to add to our own CLAP synths (in `~/Dev-Audio`) so
Umpire can use them. Few plugins support MIDI 2.0 yet, and the aim is to be
first. Feature numbers refer to the "MIDI 2.0-specific features" list in
[ideas.md](ideas.md).

State as of 2026-10-07: the plugins handle MIDI 2.0 notes with Pitch 7.9
attributes.

## Step 0: groundwork

- **Get MIDI 2.0 into StruckSynth and IFSynth.** They use DPF's generated
  CLAP wrapper, which only advertises the MIDI and MPE note formats and
  discards `CLAP_EVENT_MIDI2` (`case CLAP_EVENT_MIDI2: break;` in
  `dpf/distrho/src/DistrhoPluginCLAP.cpp`). No UMP reaches them today, so
  they never see 7.9 pitch, 16-bit velocity or anything else from MIDI 2.0.
  PlainOldSynth and PolygraphModularUnified avoid this with their own raw
  CLAP entry point (see the header comment in `PlainOldSynth/src/plugin.cpp`).
  Options:
  - give StruckSynth and IFSynth the same raw CLAP entry point, or
  - patch DPF's CLAP wrapper to advertise `CLAP_NOTE_DIALECT_MIDI2` and
    forward the events, and send the patch upstream so every DPF plugin
    gains MIDI 2.0.
- **Make the UMP decoder look at message type and status.** `decodeUmp`
  (`pm_midi.cpp`) reads every message with the MIDI 2.0 note layout. Per-note
  pitch bend, registered and assignable controllers, per-note management and
  MIDI 1.0 messages wrapped in UMP (32-bit, type 0x2) all lay their fields
  out differently. `UmpMessage` has no per-note fields yet.

## Tier 1: the core of per-note expression

- **#6, per-note pitch bend.** The biggest win: per-note glides and vibrato on
  one channel, added on top of 7.9's starting pitch. PitchConverterModule
  already turns 7.9 into a pitch CV per voice, and per-note bend adds onto
  that CV.
- **#8, per-note management.** Makes the link between note and voice
  explicit. The detach flag lets a release tail keep its expression, and the
  reset flag restores defaults on retrigger. Do it together with #6.
- **#1 and #2, full resolution all the way through.** The decoder keeps
  16-bit velocity and 32-bit values. Check that they reach the voices without
  being reduced to 7 bits, and smooth the 32-bit values. CVMixerModule
  handles channel pressure and pitch bend, but VoiceManager only routes note
  on, note off and control change.

## Tier 2: per-note controllers (#7)

- **A few registered per-note controllers that match each synth.** Useful
  ones are #1 Modulation, #7 Volume, #10 Pan, #11 Expression and #74
  Brightness. For example, filter cutoff per note in PlainOldSynth, and strike
  hardness or damping per note in StruckSynth.
- **32-bit polyphonic pressure**, the most common per-note controller.
- **Pitch 7.25 (registered per-note controller #3).** The continuous partner
  of 7.9: 7.9 sets a note's starting pitch, and 7.25 changes a note's
  absolute pitch while it plays.
- **Assignable per-note controllers.** Let any per-voice parameter be
  controlled per note.

## Tier 3: freshest, and the most characteristic of our synths

- **#9, articulation as a note attribute.** Use the manufacturer-specific
  attribute (type 1, 16 bits) to send a parameter with each note: strike
  position or mallet type on StruckSynth, a modal preset on IFSynth. Each
  note carries its own timbre with no keyswitches, and Umpire can show it as
  a property of the note. A note carries only one attribute, so this can't
  share a note with 7.9; pitch would then come from 7.25 (#3) or per-note
  pitch bend.
- **#3, registered and assignable controllers as parameter addresses.**
  Expose each plugin parameter as a 32-bit assignable controller, so
  hardware MIDI 2.0 controllers reach the synths the same way Umpire does,
  with full resolution and no MIDI learn.
- **Voice assignment from the controller** (ideas.md, "Voice assignment by
  the controller"; added 2026-10-22). Read a voice number from each note,
  most likely an assignable per-note controller sent just before the
  note-on, and let the voice manager use it: a note naming a sounding voice
  takes it over and glides from that voice's pitch; a note naming a free
  voice starts fresh. Decide what a voice number beyond the voice count
  does. Needs the decoder work (Step 0) and the per-note controller layer
  (Tier 2) first, and StruckSynth and IFSynth need MIDI 2.0 at all (Step 0).

## Low priority

- **#5, bank and program in one message.** Maps to presets.
- **#4, relative controllers.** Suit encoders more than synths.

## Not for the plugins

- **#10–17** (groups, endpoints, Product Instance ID, JR timestamps, Flex Data,
  clip messages, 8-bit SysEx): these belong to Umpire's link to hardware.
  CLAP has its own note ports and transport.
- **#18–21** (MIDI-CI): CLAP already has plugin equivalents. The params
  extension gives names and ranges (like Property Exchange's controller
  lists), the state extension gives total recall, preset discovery gives
  program lists, and remote controls give mapping pages (like profiles).
  Plugins don't need MIDI-CI over SysEx. Instead, Umpire should treat
  MIDI-CI devices and CLAP extensions as one model, so a hardware synth and
  one of our plugins look the same in its editors.
- **#22–23**: Umpire's job.

## Suggested order

1. Step 0.
2. #6 and #8 together.
3. The per-note controller layer (#7).

With 7.9, that gives a complete per-note pitch and expression path, which
hardly any other plugin has. Then #9 on StruckSynth, the feature only our
synths would have.
