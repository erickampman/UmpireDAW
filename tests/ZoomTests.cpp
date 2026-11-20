// Milestone Z1 (docs/plugin-version.md, "Z1"): horizontal zoom, as the
// plugin's lane and grid and the app's track view and grid use it
// (src/Ui/Zoom.h). Zooming about the pointer keeps the time under it in
// place, in and out; the range is an eighth to eight times; a notch is 1.25x;
// a pinch's delta, as the pugl fork sends it, follows the fingers; a flick's
// notches are capped; what a corrupt state holds reads as 1. And scrolling's
// axis lock (src/Ui/ScrollAxis.h): a two-finger drag keeps to the axis it
// started on. Silent: no window, no sound.

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include "App/Clip.h"
#include "Ui/ScrollAxis.h"
#include "Ui/Zoom.h"

namespace {

using umpire::Tick;
using umpire::kTicksPerQuarter;
using umpire::ui::Zoom;

int failures = 0;

void check(const char* what, bool ok, const std::string& detail = {}) {
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str());
    if (!ok) ++failures;
}

bool near(double a, double b, double eps = 1e-3) { return std::abs(a - b) <= eps; }

// A view as the canvases keep one: a base width a beat, a factor, and a
// scroll in logical pixels from the content's origin (the lane's
// LaneBand::tickX and tickAt, the track view's, the grid's in sixteenths).
struct View {
    float base = 16, factor = 1, scroll = 0;
    float beatW() const { return base * factor; }
    double tickAt(float pointer) const { return (pointer + scroll) / beatW() * kTicksPerQuarter; }
    float  xOf(double tick) const { return static_cast<float>(tick / kTicksPerQuarter) * beatW() - scroll; }
    void   zoom(double notches, float pointer) {
        const float before = beatW();
        factor             = Zoom::by(factor, notches);
        scroll             = Zoom::about(scroll, pointer, before, beatW());
    }
};

// What pugl's magnifyWithEvent: sends as dy for a magnification m.
double pinch(double m) { return std::log(1.0 + m) / std::log(1.25); }

} // namespace

int main() {
    {   // a notch is 1.25x, up zooms in
        check("a notch up zooms in 1.25x", near(Zoom::by(1, 1), 1.25));
        check("a notch down zooms out 0.8x", near(Zoom::by(1, -1), 0.8));
        check("no notches, no change", Zoom::by(2.5f, 0) == 2.5f);
        check("a notch in and out comes back", near(Zoom::by(Zoom::by(1, 1), -1), 1));
    }
    {   // the range: an eighth to eight times
        float f = 1;
        for (int i = 0; i < 40; ++i) f = Zoom::by(f, 1);
        check("zooming in stops at 8x", f == Zoom::kMax, std::to_string(f));
        for (int i = 0; i < 80; ++i) f = Zoom::by(f, -1);
        check("zooming out stops at 1/8x", f == Zoom::kMin, std::to_string(f));
        check("a factor out of range is clamped", Zoom::clamp(100) == 8 && Zoom::clamp(0.01f) == 0.125f &&
                                                      Zoom::clamp(-1) == 0.125f && Zoom::clamp(3) == 3);
        check("not a number is 1", Zoom::clamp(std::nanf("")) == 1 &&
                                       Zoom::clamp(std::numeric_limits<float>::infinity()) == 1);
        check("notches that aren't a number change nothing", Zoom::by(2, std::nan("")) == 2);
    }
    {   // a flick: at most four notches an event
        check("a flick is capped at four notches", near(Zoom::by(1, 50), std::pow(1.25, 4)) &&
                                                       near(Zoom::by(1, -50), std::pow(1.25, -4)));
    }
    {   // a pinch: magnification m scales by 1 + m, and spreading zooms in
        check("a pinch out of 10% zooms in 1.1x", near(Zoom::by(1, pinch(0.1)), 1.1));
        check("a pinch in of 10% zooms out 0.9x", near(Zoom::by(1, pinch(-0.1)), 0.9));
        float f = 1;
        for (int i = 0; i < 20; ++i) f = Zoom::by(f, pinch(0.02));   // twenty small steps, as a gesture sends them
        check("a gesture's steps multiply", near(f, std::pow(1.02, 20)), std::to_string(f));
    }
    {   // what's under the pointer stays there
        View v;
        v.scroll           = 300;
        const float  px    = 250;
        const double under = v.tickAt(px);
        v.zoom(3, px);
        check("zooming in keeps the time under the pointer", near(v.tickAt(px), under, 1), std::to_string(v.tickAt(px) - under));
        check("and its pixel", near(v.xOf(under), px, 1e-2));
        v.zoom(-4, px);
        v.zoom(-2, px);
        check("zooming out too", near(v.tickAt(px), under, 1));
        v.zoom(3, px);
        check("in and out by as many notches comes back", near(v.factor, 1) && near(v.scroll, 300, 1e-2),
              std::to_string(v.scroll));
    }
    {   // at the range's end, nothing moves
        View v;
        v.factor = Zoom::kMax;
        v.scroll = 1234;
        v.zoom(2, 100);
        check("at 8x, zooming in leaves the scroll", v.factor == Zoom::kMax && v.scroll == 1234);
    }
    {   // the pointer at the view's left edge: the left edge stays
        View v;
        v.scroll = 160;   // bar 3 at the left, at 16 px a beat
        v.zoom(2, 0);
        check("about the left edge, the left edge's time stays", near(v.tickAt(0), 10 * kTicksPerQuarter, 1));
    }
    {   // the grid's sixteenth (GridCanvas::kStepW 22) in the same way
        View g;
        g.base             = 22 * 4;   // a beat of sixteenths
        g.scroll           = 0;
        const double under = g.tickAt(400);
        g.zoom(-5, 400);
        check("the grid, zoomed out about the pointer", near(g.tickAt(400), under, 1));
    }
    {   // what zooms
        check("Command zooms", Zoom::zooms(true, false));
        check("a pinch zooms", Zoom::zooms(false, true));
        check("a plain wheel doesn't", !Zoom::zooms(false, false));
        check("notches: the vertical delta", Zoom::notches(0.5, 2) == 2);
        check("notches: a sideways delta if that's all", Zoom::notches(-1.5, 0) == -1.5);
    }
    {   // bad widths change nothing
        check("a zero width leaves the scroll", Zoom::about(50, 10, 0, 16) == 50 && Zoom::about(50, 10, 16, 0) == 50);
    }
    {   // a two-finger drag keeps to one axis
        using umpire::ui::ScrollAxis;
        using Axis = ScrollAxis::Axis;
        ScrollAxis    a;
        std::uint32_t t        = 1000;
        bool          vertical = true;
        // A vertical drag, as a trackpad sends it: a little sideways in most
        // events, sometimes more sideways than up for one event.
        const double drag[][2] = {{0.05, 0.4}, {0.2, 0.9}, {0.3, 1.2}, {0.4, 0.3}, {0.1, 0.8}, {0.25, 0.0}, {0.02, 0.1}};
        for (const auto& e : drag) vertical = a.axisFor(e[0], e[1], t += 16) == Axis::Vertical && vertical;
        check("a vertical two-finger drag stays vertical", vertical);
        t += 400;   // a pause: a new gesture
        bool sideways = a.axisFor(0.8, 0.1, t) == Axis::Horizontal;
        for (const auto& e : drag) sideways = a.axisFor(e[1], e[0], t += 16) == Axis::Horizontal && sideways;
        check("after a pause, a sideways one stays sideways", sideways);
        t += 400;
        check("a tie starts vertical", a.axisFor(0.5, 0.5, t) == Axis::Vertical);
        check("a tilt wheel's whole notch switches it", a.axisFor(1, 0, t += 30) == Axis::Horizontal);
        check("a stray fraction doesn't", a.axisFor(0, 0.4, t += 30) == Axis::Horizontal);
        check("nor does an event with nothing in it", a.axisFor(0, 0, t += 30) == Axis::Horizontal);
        check("time going backwards starts a new gesture", a.axisFor(0.1, 0.6, t - 5000) == Axis::Vertical);
        ScrollAxis b;
        check("a mouse wheel's first notch is vertical", b.axisFor(0, -1, 7) == Axis::Vertical);
    }
    std::printf("%s\n", failures == 0 ? "all passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
