#pragma once

// Which way a wheel or two-finger scroll moves a two-way view (the grid,
// the app's track view): one axis a gesture (2026-11-20, after the author
// found the grid's vertical two-finger scroll sliding sideways in Logic).
//
// A trackpad's two-finger drag is never quite straight: a vertical one
// carries a little sideways delta in most of its events. Taking any sideways
// delta as a sideways scroll (as the grid and track view did) turned most of
// a vertical drag into small sideways steps. So a gesture keeps the axis its
// first event chose (the larger delta; vertical on a tie) for as long as its
// events keep coming, momentum included, and the other axis's delta is
// dropped. A gap of kGestureGapMs starts a new gesture.
//
// A mouse's tilt wheel, or a sideways swipe that comes right after a
// vertical one, sends whole notches on the other axis and nothing on the
// locked one: that switches the lock, since a trackpad's stray sideways
// delta is a fraction of a notch.
//
// No toolkit here, for the tests: the canvases pass the event's deltas (in
// notches, as pugl gives them) and time (ms).

#include <cmath>
#include <cstdint>

namespace umpire::ui {

class ScrollAxis {
public:
    enum class Axis { Vertical, Horizontal };
    static constexpr std::uint32_t kGestureGapMs = 250;

    // The axis this event scrolls; its delta on the other is to be dropped.
    Axis axisFor(double dx, double dy, std::uint32_t timeMs) {
        const bool fresh = !started || timeMs < lastMs || timeMs - lastMs > kGestureGapMs;
        started = true;
        lastMs  = timeMs;
        const double ax = std::abs(dx), ay = std::abs(dy);
        if (fresh) {
            locked = ax > ay ? Axis::Horizontal : Axis::Vertical;
        } else {
            const double on = locked == Axis::Vertical ? ay : ax, off = locked == Axis::Vertical ? ax : ay;
            if (on == 0 && off >= 1) locked = locked == Axis::Vertical ? Axis::Horizontal : Axis::Vertical;
        }
        return locked;
    }

private:
    Axis          locked  = Axis::Vertical;
    std::uint32_t lastMs  = 0;
    bool          started = false;
};

} // namespace umpire::ui
