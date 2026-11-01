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
    // Clicks half a second apart, so two aren't a double click.
    void click(double x, double y) {
        time += 500;
        motion(x, y);
        button(x, y, true);
        button(x, y, false);
        pump(80);
    }
    void doubleClick(double x, double y) {
        time += 500;
        motion(x, y);
        for (int i = 0; i < 2; ++i) {
            button(x, y, true);
            button(x, y, false);
        }
        pump(80);
    }
    void drag(double x0, double y0, double x1, double y1) {
        time += 500;
        motion(x0, y0);
        button(x0, y0, true);
        for (int i = 1; i <= 4; ++i) motion(x0 + (x1 - x0) * i / 4, y0 + (y1 - y0) * i / 4);
        button(x1, y1, false);
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
        motion(x, y);   // the pointer is there first, as with a real wheel
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

#ifdef __APPLE__
    static constexpr std::uint32_t kCommand = dgl::kModifierSuper;
#else
    static constexpr std::uint32_t kCommand = dgl::kModifierControl;
#endif

    void run() {
        pump(1000);
        std::printf("      scale factor %.2f, window %ux%u\n", sf(), window.getWidth(), window.getHeight());
        screenshot("t01-empty.ppm");
        {
            // Real input (MIDI on Umpire's inputs, keys in the window) draws
            // frames too: the detail says if any arrived.
            const auto changes = controller.version();
            const auto midi    = view().monitorCount;
            const auto idle    = framesDuring(1500);
            check("no frames while idle (on demand)", idle == 0,
                  std::to_string(idle) + " in 1.5 s; the view changed " + std::to_string(controller.version() - changes) +
                      " times, " + std::to_string(view().monitorCount - midi) + " MIDI messages arrived");
        }
        check("an empty session shows only the prompt",
              view().rows.empty() && panels.rowRects.empty() && view().status == "Play something to start");

        // --- playing makes the first track
        controller.receive(1, uw::ump::midi2NoteOn(0, 0, 60, 0x8000));
        pump(100);
        check("playing makes the first track, touched, its header and lane drawn",
              view().rows.size() == 1 && view().rows[0].touched && panels.rowRects.size() == 1);
        if (view().rows.empty() || panels.rowRects.empty()) return;
        const TrackId t1 = view().rows[0].id;
        {
            const auto* r = rects(t1);
            check("the header lines up with the lane", std::abs(r->row.y0 - canvas.rowY(0) * sf()) < 1.0 &&
                                                           std::abs(r->row.y1 - r->row.y0 - TrackCanvas::kRowH * sf()) < 1.5);
        }

        // --- adding tracks: the +, the Track menu, the key
        click(panels.addTrackRect);
        check("the + adds a track, touched", view().rows.size() == 2 && view().rows[1].touched);
        click(panels.trackMenuRect);
        pump(100);
        check("the Track menu opens", panels.popupOpen());
        tap(dgl::kKeyEscape);
        check("Escape closes it", !panels.popupOpen());
        tap('t', kCommand);
        check("the add-track key adds one", view().rows.size() == 3);

        // --- touching: a lane, a header, the arrow keys
        click(laneX(), laneY(0));
        check("a click on a lane touches its track", controller.selected() == t1 && probe.canvasPresses >= 1);
        tap(dgl::kKeyDown);
        check("Down touches the next track", controller.selected() == view().rows[1].id);
        {
            const auto* r = rects(t1);
            check("an untouched row shows no controls", r->arm.x1 == 0 && r->mute.x1 == 0);
            click(r->row.x0 + 6 * sf(), r->row.y1 - 3 * sf());
            check("a click on a header touches its track", controller.selected() == t1);
        }

        // --- the touched row's controls
        click(rects(t1)->arm);
        check("R arms the track", view().rows[0].armed);
        tap('r');
        check("the R key disarms the touched track", !view().rows[0].armed);
        click(rects(t1)->mute);
        check("M mutes it", view().rows[0].muted && view().undoLabel == "Mute track");
        click(rects(t1)->name.x1 - 4 * sf(), rects(t1)->name.cy());
        check("a click on the name edits it", panels.textActive());
        type(" lead");
        tap(dgl::kKeyEnter);
        check("typing and Enter rename it", view().rows[0].name == "Track 1 lead", "\"" + view().rows[0].name + "\"");
        tap(dgl::kKeyUp);
        check("keys go back to the canvas after", !panels.textActive() && controller.selected() == t1);
        tap('z', kCommand);
        check("the undo key takes the name back", view().rows[0].name == "Track 1");
        tap('z', kCommand | dgl::kModifierShift);
        check("and the redo key puts it back", view().rows[0].name == "Track 1 lead");
        click(panels.editMenuRect);
        pump(100);
        check("the Edit menu opens", panels.popupOpen());
        tap(dgl::kKeyEscape);
        tap(dgl::kKeyDown);
        screenshot("t02-rows.ppm");   // Track 1 lead untouched, with its M badge

        // --- MIDI arriving: the touched track's light, the monitor, then idle again
        {
            const auto before = view().monitorCount;
            controller.receive(1, uw::ump::midi2NoteOn(0, 0, 60, 0x8000));
            pump(40);
            check("MIDI lights the touched track only", view().rows[1].midiActive && !view().rows[0].midiActive &&
                                                          view().monitorCount == before + 1);
            pump(400);
            check("the light goes out", !view().rows[1].midiActive);
            const auto idle = framesDuring(800);
            check("and the window is idle again", idle == 0, std::to_string(idle) + " in 0.8 s");
        }

        // --- recording: arm, the record key, notes, the take growing, stop
        click(laneX(), laneY(0));
        tap('r');
        tap('r', kCommand);
        check("the record key plays and records", view().transport.playing && view().transport.recording &&
                                                    view().status == "Recording to Track 1 lead");
        for (int key : {60, 64, 67}) {
            controller.receive(1, uw::ump::midi2NoteOn(0, 0, static_cast<std::uint8_t>(key), 0x8000));
            pump(150);
            controller.receive(1, uw::ump::midi2NoteOff(0, 0, static_cast<std::uint8_t>(key), 0));
            pump(100);
        }
        check("the take grows on the armed lane", !view().rows[0].clips.empty() && view().rows[0].clips[0].recording &&
                                                     view().rows[0].clips[0].notes->size() == 3);
        {
            const auto frames = framesDuring(1000);
            check("while playing it draws about 30 frames a second", frames >= 15 && frames <= 45,
                  std::to_string(frames) + " in 1 s");
        }
        screenshot("t03-recording.ppm");
        tap(' ');
        check("Space stops, and the take is a clip", !view().transport.playing && view().rows[0].clips.size() == 1 &&
                                                       !view().rows[0].clips[0].recording && view().undoLabel == "Record");
        tap(dgl::kKeyHome);
        check("Home goes to the start", view().transport.position == 0);

        // --- the clip: select, drag along the lane, undo, delete, undo
        {
            const ClipModel clip = view().rows[0].clips[0];
            const double    cx = (canvas.tickX(clip.start) + 6) * sf(), cy = laneY(0);
            click(cx, cy);
            check("a click selects the clip", controller.selectedClip() == clip.ref && view().rows[0].clips[0].selected);
            drag(cx, cy, cx + TrackCanvas::kBeatW * 4 * sf(), cy);
            check("a drag moves it along the lane, to the beat",
                  view().rows[0].clips[0].start == clip.start + kTicksPerQuarter * 4 && view().undoLabel == "Move clip",
                  std::to_string(view().rows[0].clips[0].start));
            screenshot("t04-clip.ppm");
            tap('z', kCommand);
            check("undo moves it back", view().rows[0].clips[0].start == clip.start);
            tap(dgl::kKeyDelete);
            check("Delete deletes it", view().rows[0].clips.empty() && controller.selectedClip() == 0);
            tap('z', kCommand);
            check("undo brings it back", view().rows[0].clips.size() == 1);
            click(cx, cy);   // selected again: MainWindow's test opens it
            check("a click selects it again", controller.selectedClip() == clip.ref);
        }

        // --- the ruler and the transport keys
        click((canvas.tickX(kTicksPerQuarter * 8) + 3) * sf(), (canvas.rulerY() + TrackCanvas::kRulerH / 2) * sf());
        check("a click on the ruler moves the playhead, to the beat", view().transport.position == kTicksPerQuarter * 8 &&
                                                                       view().transport.positionLabel == "3.1.1");
        tap(' ');
        pump(300);
        check("Space plays", view().transport.playing && view().transport.position > kTicksPerQuarter * 8);
        tap(' ');
        check("and stops", !view().transport.playing);
        canvas.scrollX = TrackCanvas::kBeatW * 40;   // as dragged or played well along
        canvas.refresh();
        tap(dgl::kKeyHome);
        check("Home goes to the start and scrolls the lanes back to show it",
              view().transport.position == 0 && canvas.scrollX == 0);
        canvas.scrollX = TrackCanvas::kBeatW * 40;
        canvas.refresh();
        click((canvas.tickX(kTicksPerQuarter * 44) + 3) * sf(), (canvas.rulerY() + TrackCanvas::kRulerH / 2) * sf());
        controller.locate(kTicksPerQuarter * 8);   // a jump back, as the loop's wrap is
        pump(50);
        check("a jump back past the left edge scrolls back, two beats in",
              view().transport.position == kTicksPerQuarter * 8 && canvas.scrollX == TrackCanvas::kBeatW * 6);
        tap(dgl::kKeyHome);

        // --- the loop: a drag along the ruler, the L key, the button
        {
            const double ry = (canvas.rulerY() + TrackCanvas::kRulerH / 2) * sf();
            drag((canvas.tickX(kTicksPerQuarter * 4) + 3) * sf(), ry, (canvas.tickX(kTicksPerQuarter * 12) - 3) * sf(), ry);
            check("a drag along the ruler sets the loop, to the beat, and loops",
                  view().transport.looping && view().transport.loopStart == kTicksPerQuarter * 4 &&
                      view().transport.loopEnd == kTicksPerQuarter * 12 && view().undoLabel == "Set loop");
            screenshot("t06-loop.ppm");
            tap('l');
            check("L turns looping off, the range kept", !view().transport.looping && view().transport.loopEnd == kTicksPerQuarter * 12);
            click(panels.loopRect);
            check("the Loop button turns it on", view().transport.looping);
            tap(' ');
            pump(2200);   // beats 0 to 4, then round the loop (4 to 12) at 120
            check("playing, the playhead wraps at the loop's end",
                  view().transport.playing && view().transport.position >= kTicksPerQuarter * 4 &&
                      view().transport.position < kTicksPerQuarter * 12);
            tap(' ');
            click(panels.loopRect);
            check("and off", !view().transport.looping);
            tap(dgl::kKeyHome);
        }

        // --- the touched row's output: the choice opens (nothing is chosen: that would sound)
        {
            click(laneX(), laneY(0));
            const auto* r = rects(view().rows[0].id);
            check("the touched row shows its output", r != nullptr && panels.outputRect.x1 > panels.outputRect.x0 &&
                                                          view().rows[0].outputLabel == "No output");
            click(panels.outputRect);
            pump(100);
            check("its choice opens", panels.popupOpen());
            screenshot("t07-output.ppm");
            tap(dgl::kKeyEscape);
            check("Escape closes it, nothing chosen", !panels.popupOpen() && controller.session().outputs.empty());
            if (!view().rows[0].clips.empty()) controller.selectClip(view().rows[0].clips[0].ref);   // for MainWindow's test
        }

        // --- many tracks: the wheel scrolls headers and lanes together
        while (view().rows.size() < 16) controller.addTrack();
        pump(100);
        {
            const float before = canvas.scroll();
            scroll(laneX(), laneY(1), -2);
            const float afterLanes = canvas.scroll();
            scroll(TrackCanvas::kHeaderW * sf() / 2, (canvas.lanesY() + TrackCanvas::kRowH * 2) * sf(), -1);
            const float afterHeaders = canvas.scroll();
            check("the wheel over the lanes scrolls", afterLanes > before);
            check("the wheel over the headers scrolls the same rows", afterHeaders > afterLanes);
            const auto& first = panels.rowRects.front();
            int index = 0;
            for (std::size_t i = 0; i < view().rows.size(); ++i)
                if (view().rows[i].id == first.id) index = static_cast<int>(i);
            check("scrolled, headers still line up with lanes", std::abs(first.row.y0 - canvas.rowY(index) * sf()) < 1.0);
            screenshot("t05-scrolled.ppm");
        }

        // --- the monitor's View toggle, a resize
        panels.showMonitor = false;
        panels.wake();
        pump(100);
        check("hiding the monitor gives the lanes the room, above the status line",
              std::abs(canvas.lanesY() + canvas.lanesH() - (window.getHeight() - panels.statusHeight()) / sf()) < 1.0);
        panels.showMonitor = true;
        window.setSize(static_cast<uint>(std::lround(1000 * sf())), static_cast<uint>(std::lround(600 * sf())));
        pump(400);
        check("after a resize the panels cover the window",
              panels.getWidth() == window.getWidth() && panels.getHeight() == window.getHeight());

        controller.removeTrack(view().rows.back().id);
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
