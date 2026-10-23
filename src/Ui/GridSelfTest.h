#pragma once

#include <string>

#include "Application.hpp"
#include "Window.hpp"

#include "App/GridModel.h"
#include "Ui/Probe.h"

namespace umpire::ui {

class GridCanvas;

// The test grid window's part of MainWindow::selfTest(): scripted input through the same path the window
// uses (the top-level widget's handlers). Screenshots (PPM) go to `dir`.
int runGridSelfTest(DGL_NAMESPACE::Application& app, DGL_NAMESPACE::Window& window, GridCanvas& canvas, GridModel& model,
                Probe& probe, const std::string& dir);

} // namespace umpire::ui
