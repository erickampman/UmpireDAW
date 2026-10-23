#pragma once

#include <cstdint>

namespace umpire::ui {

// Where input went and how often the window drew: the self-test checks these,
// and View > Diagnostics shows them.
struct Probe {
    int canvasPresses = 0, imguiPresses = 0;   // mouse presses, by who took them
    int canvasMotions = 0;                     // motion events the canvas handled
    int canvasKeys = 0, imguiKeys = 0;         // key presses, by who took them
    int canvasScrolls = 0, imguiScrolls = 0;

    int framesLastSecond = 0;   // updated by MainWindow::idle()
};

} // namespace umpire::ui
