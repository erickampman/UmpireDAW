#pragma once

#include <string>

#include "Application.hpp"
#include "Window.hpp"

#include "App/ViewModel.h"
#include "Ui/Probe.h"

namespace umpire::ui {

class GridCanvas;

// MainWindow::selfTest(): scripted input through the same path the window
// uses (the top-level widget's handlers). Screenshots (PPM) go to `dir`.
int runSelfTest(DGL_NAMESPACE::Application& app, DGL_NAMESPACE::Window& window, GridCanvas& canvas, ViewModel& model,
                Probe& probe, const std::string& dir);

} // namespace umpire::ui
