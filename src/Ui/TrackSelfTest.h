#pragma once

#include <functional>
#include <string>

#include "Window.hpp"

#include "App/TrackController.h"
#include "Ui/Probe.h"

namespace umpire::ui {

class TrackCanvas;

// The track view's part of MainWindow::selfTest(): scripted input through the
// window's own handlers, and MIDI injected as if it had arrived. `pump` runs
// the app's loop once (the controller's tick, then the windows').
// Screenshots (PPM) go to `dir`. Returns 0 if every check passed.
int runTrackSelfTest(const std::function<void()>& pump, DGL_NAMESPACE::Window& window, TrackCanvas& canvas,
                     TrackController& controller, Probe& probe, const std::string& dir);

} // namespace umpire::ui
