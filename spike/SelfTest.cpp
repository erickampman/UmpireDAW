#include "SelfTest.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

#include "DearImGui/imgui_internal.h"

#include "ImGuiLayer.h"
#include "NoteCanvas.h"

namespace umpire::spike {

namespace dgl = DGL_NAMESPACE;
using Clock = std::chrono::steady_clock;

struct SelfTest {
    dgl::Application& app;
    dgl::Window&      window;
    NoteCanvas&       canvas;
    ImGuiLayer&       layer;
    Model&            model;
    Probe&            probe;
    std::string       dir;
    int               failures = 0;
    std::uint32_t     time     = 1000;
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
            app.idle();
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            if (ms > longestIdleMs) longestIdleMs = ms;
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    }

    void screenshot(const char* name) {
        const std::string path = dir + "/" + name;
        layer.captureNextFrame(path.c_str());
        layer.wake(1);
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
    bool button(double x, double y, bool press, std::uint32_t which = dgl::kMouseButtonLeft) {
        dgl::Widget::MouseEvent ev;
        ev.time   = ++time;
        ev.button = which;
        ev.press  = press;
        ev.pos = ev.absolutePos = dgl::Point<double>(x, y);
        return canvas.onMouse(ev);
    }
    void click(double x, double y) {
        motion(x, y);
        button(x, y, true);
        button(x, y, false);
    }
    bool key(std::uint32_t k, bool press) {
        dgl::Widget::KeyboardEvent ev;
        ev.time  = ++time;
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
        ev.time  = ++time;
        ev.pos = ev.absolutePos = dgl::Point<double>(x, y);
        ev.delta = dgl::Point<double>(0, dy);
        return canvas.onScroll(ev);
    }

    // The centre of a grid cell, in window pixels.
    double cellX(int step) { return (canvas.gridX() + (step + 0.5) * NoteCanvas::kStepW) * canvas.sf(); }
    double cellY(int pitch) {
        return (canvas.gridY() + canvas.rowY(pitch) - canvas.scrollY + NoteCanvas::kRowH / 2) * canvas.sf();
    }
    double panelX() { return window.getWidth() - layer.panelWidth() / 2; }

    std::uint64_t framesDuring(int ms) {
        const std::uint64_t before = probe.frames;
        pump(ms);
        return probe.frames - before;
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
            layer.everyFrame = true;
            const auto stock = framesDuring(1000);
            check("stock DPF-Widgets behaviour redraws continuously", stock >= 30, std::to_string(stock) + " in 1 s");
            layer.everyFrame = false;
            pump(200);
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

        // --- the text field: typing goes to ImGui, not the canvas's shortcuts
        click(layer.nameFieldRect.x1 - 4, layer.nameFieldRect.cy());
        pump(100);
        check("click on the text field goes to ImGui", probe.imguiPresses == 1 && layer.textActive());
        for (char c : std::string(" two")) type(c);
        pump(100);
        check("typing edits the field", model.clipName == "Untitled clip two", "\"" + model.clipName + "\"");
        key(dgl::kKeyDelete, true);
        key(dgl::kKeyDelete, false);
        pump(100);
        check("Delete while editing stays in the field", model.notes.size() == 1 && probe.canvasKeys == 0);
        screenshot("02-editing.ppm");

        // --- a click on the canvas ends the editing; then keys reach the canvas
        click(cellX(4), cellY(67));
        pump(100);
        check("click on the canvas ends text editing", !layer.textActive() && probe.canvasPresses == 2 &&
                                                             model.notes.size() == 1 && model.selected == 0);
        key(dgl::kKeyDelete, true);
        key(dgl::kKeyDelete, false);
        pump(100);
        check("Delete then goes to the canvas", model.notes.empty() && probe.canvasKeys == 1);
        // After a click on the panel's background, keys are still the canvas's.
        click(cellX(4), cellY(67));
        click(panelX(), window.getHeight() - 10.0);
        pump(100);
        key(dgl::kKeyUp, true);
        key(dgl::kKeyUp, false);
        pump(100);
        check("keys reach the canvas after a click on the panel", model.notes.size() == 1 &&
                                                                       model.notes[0].pitch == 68);

        // --- the menu, which opens over the canvas
        click(layer.editMenuRect.cx(), layer.editMenuRect.cy());
        pump(150);
        check("click on Edit opens the menu", layer.popupOpen() && layer.editMenuOpen);
        screenshot("03-menu.ppm");
        {
            const int presses = probe.canvasPresses;
            click(cellX(30), cellY(60));
            pump(150);
            check("click on the canvas with the menu open only closes the menu",
                  !layer.popupOpen() && probe.canvasPresses == presses && model.notes.size() == 1);
            click(layer.editMenuRect.cx(), layer.editMenuRect.cy());
            pump(150);
            const bool overCanvas = layer.clearAllItemRect.x0 < (canvas.gridX() + canvas.gridW()) * canvas.sf();
            click(layer.clearAllItemRect.cx(), layer.clearAllItemRect.cy());
            pump(150);
            check("a menu item over the canvas goes to the menu", overCanvas && model.notes.empty() &&
                                                                      probe.canvasPresses == presses && !layer.popupOpen());
        }

        // --- a canvas drag that ends over the panel
        {
            const int imguiPresses = probe.imguiPresses;
            motion(cellX(4), cellY(67));
            button(cellX(4), cellY(67), true);
            motion(cellX(8), cellY(67));
            motion(panelX(), cellY(67));
            button(panelX(), cellY(67), false);
            pump(150);
            check("a canvas drag released over the panel ends the drag", !canvas.dragging() &&
                  model.notes.size() == 1 && probe.imguiPresses == imguiPresses && !layer.textActive());
        }

        // --- an ImGui drag (the velocity slider) that goes over the canvas
        {
            model.notes[0].velocity = 100;
            layer.wake();
            pump(150);
            const auto r       = layer.velocityRect;
            const int  motions = probe.canvasMotions, presses = probe.canvasPresses;
            motion(r.cx(), r.cy());
            button(r.cx(), r.cy(), true);
            pump(50);
            for (double x = r.cx(); x > cellX(2); x -= 40) {
                motion(x, cellY(60));
                pump(10);
            }
            const int hoverDuring = canvas.hoverPitch();
            button(cellX(2), cellY(60), false);
            pump(150);
            check("a slider drag over the canvas stays ImGui's",
                  probe.canvasMotions == motions && probe.canvasPresses == presses && hoverDuring == -1 &&
                      model.notes.size() == 1 && model.notes[0].velocity == 1,
                  "velocity " + std::to_string(model.notes[0].velocity));
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
            const bool layerFits = layer.getWidth() == window.getWidth() && layer.getHeight() == window.getHeight();
            check("after a resize the ImGui layer covers the window", layerFits,
                  std::to_string(window.getWidth()) + "x" + std::to_string(window.getHeight()) + " vs " +
                      std::to_string(layer.getWidth()) + "x" + std::to_string(layer.getHeight()));
            screenshot("04-resized.ppm");
        }

        // --- a scale change (as moving to another monitor would make), at a font size that shows it
        layer.fontSize         = 20.0f;
        layer.rescaleLikeStock = true;
        layer.rescale(window.getScaleFactor());
        pump(150);
        screenshot("05-rescale-stock.ppm");
        layer.rescaleLikeStock = false;
        layer.rescale(window.getScaleFactor());
        pump(150);
        screenshot("06-rescale-fixed.ppm");
        layer.fontSize = 13.0f;
        layer.rescale(window.getScaleFactor());
        pump(150);

        const auto idle = framesDuring(1000);
        check("idle again afterwards", idle == 0, std::to_string(idle) + " in 1 s");
        std::printf("%s: %d failure%s; %llu frames drawn in all; longest app.idle() %.1f ms\n",
                    failures == 0 ? "OK" : "FAILED", failures, failures == 1 ? "" : "s",
                    static_cast<unsigned long long>(probe.frames), longestIdleMs);
    }
};

int runSelfTest(dgl::Application& app, dgl::Window& window, NoteCanvas& canvas, Model& model, Probe& probe,
                const char* dir) {
    SelfTest t{app, window, canvas, canvas.layer(), model, probe, dir};
    t.run();
    return t.failures == 0 ? 0 : 1;
}

} // namespace umpire::spike
