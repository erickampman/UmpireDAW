#pragma once

// Horizontal zoom (milestone Z1, docs/plugin-version.md, "Z1"): the plugin's
// lane and grid, and the app's track view and grid windows. A view's zoom is
// a factor on its width at 1 (the lane's 16 px a beat, the grid's 22 px a
// sixteenth, the track view's 24 px a beat), from an eighth to eight times.
//
// Command-wheel (Ctrl-wheel on Linux) zooms about the pointer, a notch 1.25x,
// up to zoom in, as StageHand's sequence editor. A two-finger pinch on a Mac
// trackpad zooms too: the pugl fork passes it on as a Command-scroll flagged
// kFlagMagnify, its delta in those notches (README.fork.md in the DPF fork).
//
// No toolkit here, for the tests: the canvases turn their events into
// `zooms` and `notches`.

#include <algorithm>
#include <cmath>

namespace umpire::ui {

struct Zoom {
    static constexpr float kMin = 0.125f, kMax = 8, kNotch = 1.25f;
    // A wheel's or a pinch's event, at most this many notches (a fast
    // flick, or a mouse wheel's acceleration, doesn't jump the whole range).
    static constexpr double kMaxNotches = 4;

    // `factor` within the range; not a number (a corrupt state), 1.
    static float clamp(float factor) {
        if (!std::isfinite(factor)) return 1;
        return std::clamp(factor, kMin, kMax);
    }

    // The factor after `notches` of zooming from `factor`.
    static float by(float factor, double notches) {
        if (!std::isfinite(notches)) return clamp(factor);
        notches = std::clamp(notches, -kMaxNotches, kMaxNotches);
        return clamp(static_cast<float>(clamp(factor) * std::pow(static_cast<double>(kNotch), notches)));
    }

    // The scroll that keeps what's under `pointer` where it is when a unit's
    // width goes from `before` to `after`. Scroll and pointer are logical
    // pixels: the scroll from the content's origin to the view's left edge,
    // the pointer from that edge.
    static float about(float scroll, float pointer, float before, float after) {
        if (!(before > 0) || !(after > 0)) return scroll;
        return (scroll + pointer) * (after / before) - pointer;
    }

    // A scroll event zooms when Command is held (Ctrl on Linux) or it's a
    // pinch; `command` and `magnify` are its modifier and flag. How many
    // notches, up positive: a wheel's vertical delta, or a sideways one if
    // that's all it has.
    static bool   zooms(bool command, bool magnify) { return command || magnify; }
    static double notches(double dx, double dy) { return dy != 0 ? dy : dx; }
};

} // namespace umpire::ui
