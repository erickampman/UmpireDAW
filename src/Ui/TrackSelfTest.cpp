#include "Ui/TrackSelfTest.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include "uw/ump/Ump.h"

#include "Ui/TrackCanvas.h"
#include "Ui/TrackPanels.h"

namespace umpire::ui {

namespace dgl = DGL_NAMESPACE;
using Clock = std::chrono::steady_clock;

// Don't touch the window while it runs, or play MIDI: real input adds frames,
// events and monitor lines.
struct TrackSelfTest {
    const std::function<void()>& loop;
    dgl::Window&                 window;
    TrackCanvas&                 canvas;
    TrackPanels&                 panels;
    TrackController&             controller;
    Probe&                       probe;
    std::string                  dir;
    int                          failures = 0;
    std::uint32_t                time     = 1000;

    void check(const char* what, bool ok, const std::string& detail = {}) {
        std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str());
        if (!ok) ++failures;
    }

    void pump(int ms) {
        const auto end = Clock::now() + std::chrono::milliseconds(ms);
        while (Clock::now() < end) {
            loop();
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    }
    std::uint64_t framesDuring(int ms) {
        const std::uint64_t before = panels.framesDrawn();
        pump(ms);
        return panels.framesDrawn() - before;
    }
    void screenshot(const char* name) {
        const std::string path = dir + "/" + name;
        panels.captureNextFrame(path);
        panels.wake(1);
        pump(150);
        std::printf("      screenshot %s\n", path.c_str());
    }

    // --- synthetic events, delivered as the window delivers them
    bool motion(double x, double y) {
        dgl::Widget::MotionEvent ev;
        ev.time = ++time;
        ev.pos = ev.absolutePos = dgl::Point<double>(x, y);
        return canvas.onMotion(ev);
    }
    bool button(double x, double y, bool press) {
        dgl::Widget::MouseEvent ev;
        ev.time   = ++time;
        ev.button = dgl::kMouseButtonLeft;
        ev.press  = press;
        ev.pos = ev.absolutePos = dgl::Point<double>(x, y);
        return canvas.onMouse(ev);
    }
    void click(double x, double y) {
        motion(x, y);
        button(x, y, true);
        button(x, y, false);
        pump(80);
    }
    void click(const TrackPanels::Rect& r) { click(r.cx(), r.cy()); }
    bool key(std::uint32_t k, bool press, std::uint32_t mod = 0) {
        dgl::Widget::KeyboardEvent ev;
        ev.time  = ++time;
        ev.key   = k;
        ev.press = press;
        ev.mod   = mod;
        return canvas.onKeyboard(ev);
    }
    void tap(std::uint32_t k, std::uint32_t mod = 0) {
        key(k, true, mod);
        key(k, false, mod);
        pump(80);
    }
    void type(const std::string& text) {
        for (const char c : text) {
            key(static_cast<std::uint32_t>(c), true);
            dgl::Widget::CharacterInputEvent ev;
            ev.time      = ++time;
            ev.character = static_cast<std::uint32_t>(c);
            ev.string[0] = c;
            canvas.onCharacterInput(ev);
            key(static_cast<std::uint32_t>(c), false);
        }
        pump(100);
    }
    bool scroll(double x, double y, double dy) {
        dgl::Widget::ScrollEvent ev;
        ev.time = ++time;
        ev.pos = ev.absolutePos = dgl::Point<double>(x, y);
        ev.delta                = dgl::Point<double>(0, dy);
        const bool taken        = canvas.onScroll(ev);
        pump(80);
        return taken;
    }

    double sf() { return window.getScaleFactor(); }
    double laneX() { return (TrackCanvas::kHeaderW + 200) * sf(); }
    double laneY(int row) { return (canvas.rowY(row) + TrackCanvas::kRowH / 2) * sf(); }
    const TrackPanels::RowRects* rects(TrackId id) {
        for (const auto& r : panels.rowRects)
            if (r.id == id) return &r;
        return nullptr;
    }
    const TrackViewModel& view() { return controller.view(); }

    void run() {
        pump(600);
        std::printf("      scale factor %.2f, window %ux%u\n", sf(), window.getWidth(), window.getHeight());
        screenshot("t01-start.ppm");
        {
            const auto idle = framesDuring(1500);
            check("no frames while idle (on demand)", idle == 0, std::to_string(idle) + " in 1.5 s");
        }
        check("one track to start, its header and lane drawn", view().rows.size() == 1 && panels.rowRects.size() == 1);
        if (view().rows.empty() || panels.rowRects.empty()) return;
        const TrackId t1 = view().rows[0].id;
        {
            const auto* r = rects(t1);
            check("the header lines up with the lane", std::abs(r->row.y0 - canvas.rowY(0) * sf()) < 1.0 &&
                                                           std::abs(r->row.y1 - r->row.y0 - TrackCanvas::kRowH * sf()) < 1.5);
        }

        // --- adding tracks: the +, the Track menu, the key
        click(panels.addTrackRect);
        check("the + adds a track, selected", view().rows.size() == 2 && view().rows[1].selected);
        click(panels.trackMenuRect);
        pump(100);
        check("the Track menu opens", panels.popupOpen());
        tap(dgl::kKeyEscape);
        check("Escape closes it", !panels.popupOpen());
#ifdef __APPLE__
        tap('t', dgl::kModifierSuper);
#else
        tap('t', dgl::kModifierControl);
#endif
        check("the add-track key adds one", view().rows.size() == 3);

        // --- selecting: a lane, a header's background, the arrow keys
        click(laneX(), laneY(0));
        check("a click on a lane selects its track", controller.selected() == t1 && probe.canvasPresses >= 1);
        tap(dgl::kKeyDown);
        check("Down selects the next track", controller.selected() == view().rows[1].id);
        {
            const auto* r = rects(t1);
            click(r->row.x0 + 6 * sf(), r->row.y1 - 3 * sf());
            check("a click on a header's background selects its track", controller.selected() == t1);
        }

        // --- the header's controls
        click(rects(t1)->arm);
        check("R arms the track", view().rows[0].armed);
        tap('r');
        check("the R key disarms the selected track", !view().rows[0].armed);
        click(rects(t1)->mute);
        check("M mutes it", view().rows[0].muted);
        click(rects(t1)->name.x1 - 4 * sf(), rects(t1)->name.cy());
        check("a click on the name edits it", panels.textActive());
        type(" lead");
        tap(dgl::kKeyEnter);
        check("typing and Enter rename it", view().rows[0].name == "Track 1 lead", "\"" + view().rows[0].name + "\"");
        tap(dgl::kKeyUp);
        check("keys go back to the canvas after", !panels.textActive() && controller.selected() == t1);
        click(rects(t1)->input);
        pump(100);
        check("the input choice opens", panels.popupOpen());
        screenshot("t02-input-choice.ppm");
        tap(dgl::kKeyEscape);

        // --- MIDI arriving: the light, the monitor, then idle again
        {
            const auto before = view().monitorCount;
            controller.receive(1, uw::ump::midi2NoteOn(0, 0, 60, 0x8000));
            pump(40);
            check("MIDI lights the tracks that take it", view().rows[0].midiActive && view().monitorCount == before + 1,
                  view().monitor.empty() ? "" : view().monitor.back().tracks);
            screenshot("t03-midi.ppm");
            pump(400);
            check("the light goes out", !view().rows[0].midiActive);
            const auto idle = framesDuring(800);
            check("and the window is idle again", idle == 0, std::to_string(idle) + " in 0.8 s");
        }

        // --- many tracks: the wheel scrolls headers and lanes together
        while (view().rows.size() < 16) controller.addTrack();
        pump(100);
        {
            const float before = canvas.scroll();
            scroll(laneX(), laneY(1), -2);
            const float afterLanes = canvas.scroll();
            scroll(TrackCanvas::kHeaderW * sf() / 2, laneY(1), -1);
            const float afterHeaders = canvas.scroll();
            check("the wheel over the lanes scrolls", afterLanes > before);
            check("the wheel over the headers scrolls the same rows", afterHeaders > afterLanes);
            const auto& first = panels.rowRects.front();
            int index = 0;
            for (std::size_t i = 0; i < view().rows.size(); ++i)
                if (view().rows[i].id == first.id) index = static_cast<int>(i);
            check("scrolled, headers still line up with lanes", std::abs(first.row.y0 - canvas.rowY(index) * sf()) < 1.0);
            screenshot("t04-scrolled.ppm");
        }

        // --- the monitor's View toggle, a resize
        panels.showMonitor = false;
        panels.wake();
        pump(100);
        check("hiding the monitor gives the lanes the room",
              std::abs(canvas.lanesY() + canvas.lanesH() - window.getHeight() / sf()) < 1.0);
        panels.showMonitor = true;
        window.setSize(static_cast<uint>(std::lround(1000 * sf())), static_cast<uint>(std::lround(600 * sf())));
        pump(400);
        check("after a resize the panels cover the window",
              panels.getWidth() == window.getWidth() && panels.getHeight() == window.getHeight());

        controller.removeTrack(controller.selected());
        pump(100);
        const auto idle = framesDuring(1000);
        check("idle at the end", idle == 0, std::to_string(idle) + " in 1 s");
    }
};

int runTrackSelfTest(const std::function<void()>& pump, dgl::Window& window, TrackCanvas& canvas,
                     TrackController& controller, Probe& probe, const std::string& dir) {
    TrackSelfTest t{pump, window, canvas, canvas.panels(), controller, probe, dir};
    t.run();
    std::printf("%s: track view, %d failure%s\n", t.failures == 0 ? "OK" : "FAILED", t.failures,
                t.failures == 1 ? "" : "s");
    return t.failures == 0 ? 0 : 1;
}

} // namespace umpire::ui
