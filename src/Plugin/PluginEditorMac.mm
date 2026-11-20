// The live backing scale of the view the plugin's window is embedded in
// (PluginEditor.h, parentScale). DGL reads its scale once, when the window is
// made, and then the view isn't in the host's window yet, so it may take the
// wrong screen's; the plugin sizes the window from this instead, as Retune's
// editor does (its src/UI/BackingScale_mac.mm).

#import <Cocoa/Cocoa.h>

#include <cmath>
#include <cstdint>

#include "Plugin/PluginEditor.h"

namespace umpire::plugin {

double parentScale(std::uintptr_t parent) {
    NSView* const view = (__bridge NSView*)reinterpret_cast<void*>(parent);
    if (NSWindow* const window = view != nil ? [view window] : nil) return [window backingScaleFactor];
    NSScreen* const screen = [NSScreen mainScreen];
    return screen != nil ? [screen backingScaleFactor] : 1.0;
}

bool viewHasSize(std::uintptr_t view, unsigned width, unsigned height) {
    NSView* const v = (__bridge NSView*)reinterpret_cast<void*>(view);
    if (v == nil) return true;
    const NSSize size = [v frame].size;
    return std::fabs(size.width - width) < 0.5 && std::fabs(size.height - height) < 0.5;
}

} // namespace umpire::plugin
