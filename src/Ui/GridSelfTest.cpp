#include "Ui/GridSelfTest.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include "imgui.h"

#include "Ui/GridCanvas.h"
#include "Ui/GridPanels.h"

namespace umpire::ui {

namespace dgl = DGL_NAMESPACE;
using Clock = std::chrono::steady_clock;

// The spike's self-test (docs/ui-spike.md), over Umpire's own widget. Don't
// touch the window while it runs: real pointer motion or exposes add frames
// and events.
struct GridSelfTest {
    const std::function<void()>& idle;
    dgl::Window&      window;
    GridCanvas&       canvas;
    GridPanels&           panels;
    GridModel&        model;
    Probe&            probe;
    std::string       dir;
    int               failures      = 0;
    std::uint32_t     time          = 1000;
    double            longestIdleMs = 0;   // a swap waiting for the compositor shows here

    void check(const char* what, bool ok, const std::string& detail = {}) {
        std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str());
        if (!ok) ++failures;
    }

    // Run the event loop as the app does, drawing whatever is asked for.
    void pump(int ms) {
        const auto end = Clock::now() + std::chrono::milliseconds(ms);
        while (Clock::now() < end) {
            const auto t0 = Clock::now();
            idle();
            const double took = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            longestIdleMs     = std::max(longestIdleMs, took);
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    }

    void screenshot(const char* name) {
        const std::string path = dir + "/" + name;
        panels.captureNextFrame(path);
        panels.wake(1);
        pump(150);
        std::printf("      screenshot %s\n", path.c_str());
    }

    // --- synthetic events, delivered as the window delivers them
    bool motion(double x, double y, std::uint32_t mod = 0) {
        dgl::Widget::MotionEvent ev;
        ev.time = ++time;
        ev.mod  = mod;
        ev.pos = ev.absolutePos = dgl::Point<double>(x, y);
        return canvas.onMotion(ev);
    }
    bool button(double x, double y, bool press, std::uint32_t which = dgl::kMouseButtonLeft, std::uint32_t mod = 0) {
        dgl::Widget::MouseEvent ev;
        ev.time   = ++time;
        ev.mod    = mod;
        ev.button = which;
        ev.press  = press;
        ev.pos = ev.absolutePos = dgl::Point<double>(x, y);
        return canvas.onMouse(ev);
    }
    void click(double x, double y, std::uint32_t mod = 0) {
        motion(x, y, mod);
        button(x, y, true, dgl::kMouseButtonLeft, mod);
        button(x, y, false, dgl::kMouseButtonLeft, mod);
    }
    bool key(std::uint32_t k, bool press, std::uint32_t mod = 0) {
        dgl::Widget::KeyboardEvent ev;
        ev.time  = ++time;
        ev.mod   = mod;
        ev.key   = k;
        ev.press = press;
        return canvas.onKeyboard(ev);
    }
    void type(char c) {
        key(static_cast<std::uint32_t>(c), true);
        dgl::Widget::CharacterInputEvent ev;
        ev.time      = ++time;
        ev.character = static_cast<std::uint32_t>(c);
        ev.string[0] = c;
        canvas.onCharacterInput(ev);
        key(static_cast<std::uint32_t>(c), false);
    }
    bool scroll(double x, double y, double dy) {
        dgl::Widget::ScrollEvent ev;
        ev.time = ++time;
        ev.pos = ev.absolutePos = dgl::Point<double>(x, y);
        ev.delta                = dgl::Point<double>(0, dy);
        return canvas.onScroll(ev);
    }

    // The centre of a grid cell, in window pixels.
    double cellX(int step) { return (canvas.gridX() + (step + 0.5) * GridCanvas::kStepW) * canvas.sf(); }
    double cellY(int pitch) {
        return (canvas.gridY() + canvas.rowY(pitch) - canvas.scrollY + GridCanvas::kRowH / 2) * canvas.sf();
    }
    double panelX() { return window.getWidth() - panels.panelWidth() / 2; }
    void   press(std::uint32_t k, std::uint32_t mod = 0) {
        key(k, true, mod);
        key(k, false, mod);
        pump(100);
    }
    // The clip's notes, as the controller has it now.
    std::vector<NoteSpan> clipNotes() {
        const auto& c = model.clip();
        return c ? notesOf(c->events, c->length) : std::vector<NoteSpan>{};
    }

    std::uint64_t framesDuring(int ms) {
        const std::uint64_t before = panels.framesDrawn();
        pump(ms);
        return panels.framesDrawn() - before;
    }

    void run() {
        pump(600);
        std::printf("      scale factor %.2f, window %ux%u, ImGui %s\n", window.getScaleFactor(), window.getWidth(),
                    window.getHeight(), IMGUI_VERSION);
        screenshot("01-start.ppm");

        // --- redraws
        {
            const auto idle = framesDuring(1500);
            check("no frames while idle (on demand)", idle == 0, std::to_string(idle) + " in 1.5 s");
            motion(cellX(10), cellY(70));   // a move over the canvas: the hover row
            const auto hover = framesDuring(300);
            check("a move over the canvas draws a few frames, then stops", hover >= 1 && hover <= 4,
                  std::to_string(hover));
        }

        // --- a click on the canvas
        click(cellX(4), cellY(67));
        pump(100);
        check("click on the grid goes to the canvas (adds a note)",
              model.notes.size() == 1 && probe.canvasPresses == 1 && probe.imguiPresses == 0);
        check("the note is the clip's, an \"Add note\" command",
              clipNotes().size() == 1 && clipNotes()[0].start == 4 * GridModel::kStep && model.undoLabel == "Add note",
              "undo \"" + model.undoLabel + "\"");
        const std::string name = model.clipName;

        // --- the text field: typing goes to ImGui, not the canvas's shortcuts
        click(panels.nameFieldRect.x1 - 4, panels.nameFieldRect.cy());
        pump(100);
        check("click on the text field goes to ImGui", probe.imguiPresses == 1 && panels.textActive());
        for (char c : std::string(" two")) type(c);
        pump(100);
        check("typing edits the field, not yet the clip", panels.nameText() == name + " two" && model.clipName == name,
              "\"" + panels.nameText() + "\"");
        key(dgl::kKeyDelete, true);
        key(dgl::kKeyDelete, false);
        pump(100);
        check("Delete while editing stays in the field", model.notes.size() == 1 && probe.canvasKeys == 0);
        screenshot("02-editing.ppm");

        // --- a click on the canvas ends the editing; then keys reach the canvas
        click(cellX(4), cellY(67));
        pump(100);
        check("click on the canvas ends text editing", !panels.textActive() && probe.canvasPresses == 2 &&
                                                            model.notes.size() == 1 && model.focus == 0);
        check("and renames the clip, keeping the selection",
              model.clipName == name + " two" && model.clip()->name == model.clipName && model.undoLabel == "Rename clip" &&
                  model.notes[0].selected,
              "\"" + model.clipName + "\"");
        key(dgl::kKeyDelete, true);
        key(dgl::kKeyDelete, false);
        pump(100);
        check("Delete then goes to the canvas", model.notes.empty() && probe.canvasKeys == 1);
        check("and deletes the clip's note", clipNotes().empty() && model.undoLabel == "Delete note");
        // After a click on the panel's background, keys are still the canvas's.
        click(cellX(4), cellY(67));
        click(panelX(), window.getHeight() - 10.0);
        pump(100);
        key(dgl::kKeyUp, true);
        key(dgl::kKeyUp, false);
        pump(100);
        check("keys reach the canvas after a click on the panel",
              model.notes.size() == 1 && model.notes[0].pitch == 68);
        check("Up moves the clip's note a row", clipNotes().size() == 1 &&
                  clipNotes()[0].pitch79 == model.rowPitch79(68) && model.undoLabel == "Move note");

        // --- the menu, which opens over the canvas
        click(panels.editMenuRect.cx(), panels.editMenuRect.cy());
        pump(150);
        check("click on Edit opens the menu", panels.popupOpen() && panels.editMenuOpen);
        screenshot("03-menu.ppm");
        {
            const int presses = probe.canvasPresses;
            click(cellX(30), cellY(60));
            pump(150);
            check("click on the canvas with the menu open only closes the menu",
                  !panels.popupOpen() && probe.canvasPresses == presses && model.notes.size() == 1);
            click(panels.editMenuRect.cx(), panels.editMenuRect.cy());
            pump(150);
            const bool overCanvas = panels.clearAllItemRect.x0 < (canvas.gridX() + canvas.gridW()) * canvas.sf();
            click(panels.clearAllItemRect.cx(), panels.clearAllItemRect.cy());
            pump(150);
            check("a menu item over the canvas goes to the menu",
                  overCanvas && model.notes.empty() && probe.canvasPresses == presses && !panels.popupOpen());
            check("Edit > Delete all notes empties the clip", clipNotes().empty() && model.undoLabel == "Delete note");
        }

        // --- a canvas drag that ends over the panel: a note moved as far as it goes
        {
            click(cellX(4), cellY(67));
            pump(100);
            const int imguiPresses = probe.imguiPresses;
            motion(cellX(4), cellY(67));
            button(cellX(4), cellY(67), true);
            motion(cellX(8), cellY(67));
            motion(panelX(), cellY(67));
            button(panelX(), cellY(67), false);
            pump(150);
            check("a canvas drag released over the panel ends the drag",
                  !canvas.dragging() && model.notes.size() == 1 &&
                      probe.imguiPresses == imguiPresses && !panels.textActive());
            check("as one \"Move note\", to the grid's end", model.undoLabel == "Move note" && clipNotes().size() == 1 &&
                      clipNotes()[0].start + clipNotes()[0].length == model.endTick());
        }

        // --- an ImGui drag (the velocity knob, turned down) that goes over the canvas
        {
            model.setSelectedVelocity(100);
            panels.wake();
            pump(150);
            const auto   r       = panels.velocityRect;
            const int    motions = probe.canvasMotions, presses = probe.canvasPresses;
            const double endY    = std::min(window.getHeight() - 20.0, r.cy() + 300 * window.getScaleFactor());
            motion(r.cx(), r.cy());
            button(r.cx(), r.cy(), true);
            pump(50);
            for (int i = 1; i <= 10; ++i) {
                const double t = i / 10.0;
                motion(r.cx() + (cellX(2) - r.cx()) * t, r.cy() + (endY - r.cy()) * t);
                pump(20);
            }
            const int hoverDuring = canvas.hoverPitch();
            button(cellX(2), endY, false);
            pump(150);
            check("a knob drag over the canvas stays ImGui's",
                  probe.canvasMotions == motions && probe.canvasPresses == presses && hoverDuring == -1 &&
                      model.notes.size() == 1 && model.notes[0].velocity == GridModel::kMinVelocity,
                  "velocity " + std::to_string(model.notes[0].velocity));
            check("letting go sets the clip's velocity, once", model.undoLabel == "Set velocity" &&
                      clipNotes().size() == 1 && clipNotes()[0].velocity == (1 << 9));
            screenshot("04-knob.ppm");
        }

        // --- resizing, a box, Shift+click, Delete, undo and nudging
        {
            model.clear();
            model.commit();
            click(cellX(4), cellY(67));
            click(cellX(8), cellY(64));
            pump(100);
            // The first note's right edge (two sixteenths long), dragged to column 9.
            const double edgeX = (canvas.gridX() + 6 * GridCanvas::kStepW - 2) * canvas.sf();
            motion(edgeX, cellY(67));
            button(edgeX, cellY(67), true);
            motion(cellX(7), cellY(67));
            motion(cellX(9), cellY(67));
            button(cellX(9), cellY(67), false);
            pump(100);
            const auto notes = clipNotes();
            check("dragging a note's right edge resizes it, to a sixteenth",
                  notes.size() == 2 && notes[0].start == 4 * GridModel::kStep && notes[0].length == 6 * GridModel::kStep &&
                      model.undoLabel == "Resize note",
                  notes.empty() ? "" : "length " + std::to_string(notes[0].length));
            motion(cellX(2), cellY(70));
            button(cellX(2), cellY(70), true);
            motion(cellX(6), cellY(66));
            motion(cellX(12), cellY(62));
            screenshot("07-box.ppm");
            button(cellX(12), cellY(62), false);
            pump(100);
            check("a drag on empty grid selects what its box touches", model.selectionSize() == 2 && model.notes.size() == 2);
            press(dgl::kKeyDelete);
            check("Delete deletes the selection, as one command", clipNotes().empty() && model.undoLabel == "Delete notes");
#ifdef __APPLE__
            const std::uint32_t command = dgl::kModifierSuper;
#else
            const std::uint32_t command = dgl::kModifierControl;
#endif
            press('z', command);
            check("the undo key undoes it, and the grid follows",
                  model.notes.size() == 2 && clipNotes().size() == 2 && model.redoLabel == "Delete notes");
            click(cellX(4), cellY(67));
            click(cellX(8), cellY(64), dgl::kModifierShift);
            pump(100);
            check("Shift+click adds to the selection", model.selectionSize() == 2);
            press(dgl::kKeyRight);
            const auto moved = clipNotes();
            check("Right moves the selection a sixteenth, as one command",
                  moved.size() == 2 && moved[0].start == 5 * GridModel::kStep && moved[1].start == 9 * GridModel::kStep &&
                      model.undoLabel == "Move notes" && model.selectionSize() == 2);
            press(dgl::kKeyUp, dgl::kModifierShift);
            check("Shift+Up moves it an octave", clipNotes().size() == 2 && model.notes[0].pitch == 67 + model.edo);
            press('z', command | dgl::kModifierShift);
            check("the redo key with nothing to redo does nothing", model.notes[0].pitch == 67 + model.edo);
            press('z', command);
            check("undo puts it back", model.notes.size() == 2 && model.notes[0].pitch == 67);
        }

        // --- the wheel
        {
            const float before = canvas.scrollY;
            scroll(cellX(10), cellY(64), -1);
            scroll(panelX(), 200, -1);
            check("the wheel goes to whatever is under the pointer",
                  canvas.scrollY != before && probe.canvasScrolls == 1 && probe.imguiScrolls == 1);
        }

        // --- resizing
        {
            const double sf = window.getScaleFactor();
            window.setSize(static_cast<uint>(std::lround(900 * sf)), static_cast<uint>(std::lround(560 * sf)));
            pump(400);
            const bool fits = panels.getWidth() == window.getWidth() && panels.getHeight() == window.getHeight();
            check("after a resize the ImGui panels cover the window", fits,
                  std::to_string(window.getWidth()) + "x" + std::to_string(window.getHeight()) + " vs " +
                      std::to_string(panels.getWidth()) + "x" + std::to_string(panels.getHeight()));
            screenshot("05-resized.ppm");
        }

        // --- a rescale (as moving to another monitor would make), at a font size that shows it
        {
            const float before = panels.fontSize();
            panels.setFontSize(20.0f);
            pump(150);
            check("a rescale draws with the new font", ImGui::GetFontSize() > before * window.getScaleFactor());
            screenshot("06-rescaled.ppm");
            panels.setFontSize(before);
            pump(150);
        }

        const auto idle = framesDuring(1000);
        check("idle again afterwards", idle == 0, std::to_string(idle) + " in 1 s");
        std::printf("%s: %d failure%s; %llu frames drawn in all; longest app.idle() %.1f ms\n",
                    failures == 0 ? "OK" : "FAILED", failures, failures == 1 ? "" : "s",
                    static_cast<unsigned long long>(panels.framesDrawn()), longestIdleMs);
    }
};

int runGridSelfTest(const std::function<void()>& idle, dgl::Window& window, GridCanvas& canvas, GridModel& model,
                    Probe& probe, const std::string& dir) {
    GridSelfTest t{idle, window, canvas, canvas.panels(), model, probe, dir};
    t.run();
    return t.failures == 0 ? 0 : 1;
}

} // namespace umpire::ui
