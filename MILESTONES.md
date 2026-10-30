# Umpire milestones

## Tests
- all: ninja -C build && ctest --test-dir build --output-on-failure
- self-test: build/umpire-daw --self-test DIR (opens two windows; silent; don't type into them)

## Toward a first useful version

Record MIDI 2.0 from KeyEDO or hardware, edit it in a piano roll that keeps
per-note data, play it back to plugins or hardware, and save it. One linear
timeline. Linux first (decisions.md, "Scope").

1. [x] DGL + Dear ImGui spike (ui-spike.md): on-demand redraw, vsync,
   rescale, NanoVG canvas — done 2026-10-07
2. [x] uw-core: shared UMP, plugin seam, CLAP host, JACK/ALSA and
   CoreAudio/CoreMIDI — done 2026-10-07
3. [x] DPF fork (erickampman/DPF + pugl): upstream patches, vsync opt-in,
   per-patch README — done 2026-10-14
4. [x] Umpire skeleton (skeleton.md): CMake against uw-core and DPF fork,
   main window, view-model rule, house look and feel (KeyboardEDO's
   Widgets, DejaVu Sans, colours provisional) — done on the Mac and Linux
   2026-10-23
5. [x] Track view and MIDI in (track-view.md): track rows (input,
   channels/groups, arm, mute/solo, output, activity), lanes, ALSA UMP
   input, monitor — done on the Mac and Linux 2026-10-23
6. [x] Transport, clip model and recording (recording.md): play/stop/
   record/tempo, clip model (UMP ticks, per-note data, per-key Pitch 7.25
   with no note, Per-Note Management Reset kept), undo, session input,
   sparse rows, armed tracks record clips on lanes; double-click opens the
   grid (EDO rows) — built and self-tested on Linux 2026-10-23; the Mac
   build and recording real MIDI by hand are the author's
7. [x] Playback to MIDI outputs (uw-core, then Umpire; playback.md): ALSA
   UMP output, track output to a device, scheduler, loop/locate; chase
   per-key Pitch 7.25 on play, seek and loop wrap (the synths keep it as a
   tuning table) — built and self-tested on Linux 2026-10-29, uw-core
   9f4ee51 (scheduled output); listening to it, and the Mac build, are the
   author's
8. [ ] Hosting CLAP instruments (Umpire): track output to a plugin, JACK
   audio via uw-core, sample-accurate timing, offline-capable engine
9. [ ] Files: MIDI Clip Files (M2-116-U), session JSON, open/save dialogs
10. [ ] Clip editor (piano roll): select, move, resize, delete, velocity,
    undo; Pitch 7.9 on EDO grid
11. [ ] Plugin editor windows (uw-core, then Umpire): move X11 embedding
    from StageHand into uw-core, open from a track's row
12. [ ] UMP groups and arrangement (Umpire): tracks over group ranges,
    clip references with transforms
13. [ ] First useful version (Umpire): record from KeyEDO, edit, play to
    plugin and hardware, save/reopen

## After the first useful version: audio

Decided 2026-10-14 (decisions.md, "Audio: printing, mixdown, playback").
Plugin tracks come first; hardware comes last.

14. [ ] Mixer and offline render: per-track gain/pan/mute/solo, master bus,
    render loop; render and print start from reset keys (Per-Note
    Management Reset), not whatever played last
15. [ ] Print plugin track: render to WAV on command, staleness hash, peak
    cache
16. [ ] Audio track view: MIDI and waveform views stacked, swap front/back
17. [ ] Audio playback and mixdown: play printed audio, mix to file, play
    mix in app
18. [ ] Print hardware track: JACK capture, record with latency
    compensation

## Outside Umpire (related)
- [x] StageHand switches to uw-core (merged 2026-10-08)
- [x] StageHand switches to DPF fork (2026-10-22, StageHand b65ba55)
- [ ] Plugins move onto DPF fork and use uw::ump (plugins-roadmap.md)
