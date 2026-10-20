# Umpire milestones

## Tests
- spike: ninja -C build

## M1: Infrastructure
- [x] DGL + Dear ImGui spike: on-demand redraw, vsync, rescale, NanoVG canvas
- [x] uw-core: shared UMP, plugin seam, CLAP host, JACK/ALSA and CoreAudio/CoreMIDI
- [x] DPF fork (erickampman/DPF + pugl): upstream patches, vsync opt-in, per-patch README

## M2: MIDI DAW core
- [ ] Umpire skeleton: CMake against uw-core and DPF fork, main window, view-model rule
- [ ] House look and feel from the start: plugins' knobs, group boxes, slider, DejaVu Sans (KeyboardEDO's Widgets)
- [ ] MIDI in: ALSA sequencer UMP, clip model (UMP ticks, per-note data), live record to grid
- [ ] Clip model holds per-key Pitch 7.25 with no note; keep Per-Note Management Reset when recording
- [ ] Files: MIDI Clip Files (M2-116-U), session JSON, open/save dialogs
- [ ] Piano roll: select, move, resize, delete, velocity, undo; Pitch 7.9 on EDO grid
- [ ] Transport and MIDI out: ALSA UMP output in uw-core, play/stop/loop/tempo, scheduler
- [ ] Chase per-key Pitch 7.25 on play, seek and loop wrap (the synths keep it as a tuning table)

## M3: Plugin hosting and first release
- [ ] CLAP instruments: JACK audio via uw-core, one plugin per track, sample-accurate timing
- [ ] Plugin editor windows: move X11 embedding from StageHand into uw-core, open from track list
- [ ] Tracks and arrangement: UMP groups per track, linear clip timeline, clip transforms
- [ ] First useful version: record from KeyEDO, edit, play to plugin and hardware, save/reopen

## M4: Audio
- [ ] Mixer and offline render: per-track gain/pan/mute/solo, master bus, render loop
- [ ] Render and print start from reset keys (Per-Note Management Reset), not whatever played last
- [ ] Print plugin track: render to WAV on command, staleness hash, peak cache
- [ ] Audio track view: MIDI and waveform views stacked, swap front/back
- [ ] Audio playback and mixdown: play printed audio, mix to file, play mix in app
- [ ] Print hardware track: JACK capture, record with latency compensation

## Outside Umpire (related)
- [x] StageHand switches to uw-core (merged 2026-10-08)
- [ ] StageHand switches to DPF fork (after M1 above)
- [ ] Plugins move onto DPF fork and use uw::ump (plugins-roadmap.md)
