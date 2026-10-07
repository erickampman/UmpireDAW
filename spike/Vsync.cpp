// Kept apart from DGL's headers: X11's (Window, None, Bool...) clash with them.
#include "Vsync.h"

#include <GL/glx.h>
#include <GL/glxext.h>

namespace umpire::spike {

bool disableVsyncForCurrentContext() {
    Display* const     display  = glXGetCurrentDisplay();
    const GLXDrawable  drawable = glXGetCurrentDrawable();
    if (display == nullptr || drawable == 0) return false;
    const auto proc = [](const char* name) { return glXGetProcAddress(reinterpret_cast<const GLubyte*>(name)); };
    if (const auto ext = reinterpret_cast<PFNGLXSWAPINTERVALEXTPROC>(proc("glXSwapIntervalEXT"))) {
        ext(display, drawable, 0);
        return true;
    }
    if (const auto mesa = reinterpret_cast<PFNGLXSWAPINTERVALMESAPROC>(proc("glXSwapIntervalMESA"))) {
        return mesa(0) == 0;
    }
    return false;
}

} // namespace umpire::spike
