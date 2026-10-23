#pragma once

// Umpire's main window: the NanoVG canvas with the ImGui panels over it, in
// one DGL window. This header has no DGL or ImGui types, so the app's code
// (main.cpp) doesn't see the toolkit, as StageHand's RackUi.h keeps it.

#include <memory>
#include <string>

#include "App/ViewModel.h"

namespace umpire::ui {

class MainWindow {
public:
    struct Options {
        bool logFrames = false;   // print the frames drawn each second
    };

    // Opens the window, showing and editing `model`. Null if there's no
    // display to open it on (Linux: DISPLAY isn't set).
    static std::unique_ptr<MainWindow> open(ViewModel& model, Options options);
    ~MainWindow();

    // DGL's events and drawing: call every 10 ms or so, from the main loop.
    void idle();
    // The user closed the window or chose Quit.
    bool closed() const;

    // Scripted input through the window's own handlers, checking where each
    // event went and how many frames were drawn, with screenshots (PPM) in
    // `dir`. Runs its own loop instead of the main loop's. Returns the exit
    // status: 0 if every check passed.
    int selfTest(const std::string& dir);

private:
    struct Impl;
    explicit MainWindow(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl;
};

} // namespace umpire::ui
