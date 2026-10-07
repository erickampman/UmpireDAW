#pragma once

namespace umpire::spike {

// Turn off vsync for the GL context current on this thread (Linux, GLX).
// With vsync on, a swap on a window the compositor isn't showing (behind
// another window, on another workspace) waits up to a second under XWayland,
// stalling the thread. Returns false if it couldn't.
bool disableVsyncForCurrentContext();

} // namespace umpire::spike
