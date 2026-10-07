#pragma once

#include "Application.hpp"
#include "Window.hpp"

#include "SpikeModel.h"

namespace umpire::spike {

class NoteCanvas;

// Scripted input through the same path the window uses (the top-level
// widget's handlers), checking where each event went and how many frames were
// drawn. Screenshots (PPM) go to `dir`. Returns the exit status: 0 if every
// check passed.
int runSelfTest(DGL_NAMESPACE::Application& app, DGL_NAMESPACE::Window& window, NoteCanvas& canvas, Model& model,
                Probe& probe, const char* dir);

} // namespace umpire::spike
