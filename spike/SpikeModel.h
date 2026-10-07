#pragma once

// Milestone 1's stand-in for a view model: what the canvas and the ImGui
// panel both show and edit. No DGL or ImGui types here (decisions.md: only the
// drawing layer depends on the toolkit).

#include <cstdint>
#include <string>
#include <vector>

namespace umpire::spike {

struct Note {
    int pitch    = 60;   // a row of the grid (an EDO step)
    int start    = 0;    // in steps (sixteenths)
    int length   = 2;    // in steps
    int velocity = 100;  // 1..127, shown as the note's brightness
};

struct Model {
    std::string       clipName = "Untitled clip";
    std::vector<Note> notes;
    int               selected = -1;   // index into notes, or -1
    int               steps    = 64;   // four bars of sixteenths

    void deleteSelected() {
        if (selected < 0 || selected >= static_cast<int>(notes.size())) return;
        notes.erase(notes.begin() + selected);
        selected = -1;
    }
};

// What the spike measures: frames drawn and where each input went, so the
// self-test (and the panel) can show that redraws and routing behave.
struct Probe {
    std::uint64_t frames = 0;          // ImGui frames drawn (one per window redraw)
    int           framesLastSecond = 0;
    double        secondStart      = 0;
    int           framesThisSecond = 0;

    int canvasPresses = 0, imguiPresses = 0;     // mouse presses, by who took them
    int canvasMotions = 0;                       // motion events the canvas handled
    int canvasKeys    = 0, imguiKeys    = 0;     // key presses, by who took them
    int canvasScrolls = 0, imguiScrolls = 0;

    bool logFrames = false;   // print frames per second to stdout (--log-frames)
};

} // namespace umpire::spike
