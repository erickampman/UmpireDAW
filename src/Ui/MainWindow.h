#pragma once

// Umpire's windows: the main window (the track view: TrackCanvas with
// TrackPanels over it) and, for now, a test grid window (milestone 4's grid,
// in its own window as a clip's will open: decisions.md, "The track view").
// This header has no DGL or ImGui types, so the app's code (main.cpp) doesn't
// see the toolkit, as StageHand's RackUi.h keeps it.

#include <memory>
#include <string>

#include "App/TrackController.h"

namespace umpire::ui {

class MainWindow {
public:
    struct Options {
        bool logFrames = false;   // print the frames drawn each second
    };

    // Opens the main window over `controller`. Null if there's no display to
    // open it on (Linux: DISPLAY isn't set).
    static std::unique_ptr<MainWindow> open(TrackController& controller, Options options);
    ~MainWindow();

    // DGL's events and drawing, after the controller's tick: call every 10 ms
    // or so, from the main loop. Redraws what the controller changed.
    void idle();
    // The user closed the main window or chose Quit.
    bool closed() const;

    // Scripted input through the windows' own handlers: the track view, then
    // the test grid window. Checks where each event went and how many frames
    // were drawn, with screenshots (PPM) in `dir`. Runs its own loop, ticking
    // the controller itself. Returns the exit status: 0 if every check passed.
    int selfTest(const std::string& dir);

private:
    struct Impl;
    explicit MainWindow(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl;
};

} // namespace umpire::ui
