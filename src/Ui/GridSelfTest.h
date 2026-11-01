#pragma once

#include <functional>
#include <string>

#include "Application.hpp"
#include "Window.hpp"

#include "App/GridModel.h"
#include "Ui/Probe.h"

namespace umpire::ui {

class GridCanvas;

// A clip's grid window's part of MainWindow::selfTest(): scripted input through the same path the window
// uses (the top-level widget's handlers), editing an empty clip through the controller's commands. `idle`
// runs the app's loop once, as MainWindow's does. Screenshots (PPM) go to `dir`.
int runGridSelfTest(const std::function<void()>& idle, DGL_NAMESPACE::Window& window, GridCanvas& canvas,
                    GridModel& model, Probe& probe, const std::string& dir);

} // namespace umpire::ui
