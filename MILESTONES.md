# Umpire milestones

## Tests
- ninja -C build && ctest --test-dir build
- windows: build/umpire-daw --self-test DIR (opens two windows; silent; don't type into them)

## M1: Infrastructure
- [x] DGL + Dear ImGui spike: on-demand redraw, vsync, rescale, NanoVG canvas
- [x] uw-core: shared UMP, plugin seam, CLAP host, JACK/ALSA and CoreAudio/CoreMIDI
- [x] DPF fork (erickampman/DPF + pugl): upstream patches, vsync opt-in, per-patch README

## M2: MIDI DAW core
- [x] Umpire skeleton: CMake against uw-core and DPF fork, main window, view-model rule (Mac and Linux 2026-10-23)
- [x] House look and feel from the start: KeyboardEDO's Widgets in Umpire, DejaVu Sans (colours provisional)
- [x] Track view and MIDI in (Mac and Linux 2026-10-23): track rows (input, channels/groups, arm, mute/solo, output, activity), lanes, ALSA UMP input, monitor
- [x] Transport and recording (Linux 2026-10-23): play/stop/record/tempo, clip model (UMP ticks, per-note data), undo, session input, sparse rows, armed tracks record clips on lanes; double-click opens the grid (EDO rows)
- [x] Clip model holds per-key Pitch 7.25 with no note; keep Per-Note Management Reset when recording
- [ ] Playback to MIDI outputs: ALSA UMP output in uw-core, track output to a device, scheduler, loop/locate
- [ ] Chase per-key Pitch 7.25 on play, seek and loop wrap (the synths keep it as a tuning table)

## M3: Plugin hosting and first release
- [ ] CLAP instruments: track output to a plugin, JACK audio via uw-core, sample-accurate timing, offline-capable engine
- [ ] Files: MIDI Clip Files (M2-116-U), session JSON, open/save dialogs
- [ ] Clip editor (piano roll): select, move, resize, delete, velocity, undo; Pitch 7.9 on EDO grid
- [ ] Plugin editor windows: move X11 embedding from StageHand into uw-core, open from a track's row
- [ ] UMP groups and arrangement: tracks over group ranges, clip references with transforms
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
- [x] StageHand switches to DPF fork (2026-10-22, StageHand b65ba55)
- [ ] Plugins move onto DPF fork and use uw::ump (plugins-roadmap.md)
