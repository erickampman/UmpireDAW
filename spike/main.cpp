// umpire-ui-spike: milestone 1 (docs/decisions.md). One DGL window with a
// NanoVG note grid under a Dear ImGui menu bar and inspector, to find out
// whether DPF-Widgets' ImGui works with StageHand's DGL. Findings:
// docs/ui-spike.md.
//
//   umpire-ui-spike                  the window, for trying it by hand
//   umpire-ui-spike --log-frames     ... printing frames drawn per second
//   umpire-ui-spike --self-test DIR  scripted input, checks, screenshots in DIR
//   ... --vsync                      keep the driver's vsync (the stall: docs/ui-spike.md)

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include "Application.hpp"
#include "Window.hpp"

#include "ImGuiLayer.h"
#include "NoteCanvas.h"
#include "SelfTest.h"
#include "Vsync.h"

namespace dgl = DGL_NAMESPACE;
using namespace umpire::spike;

namespace {

class SpikeWindow final : public dgl::Window {
public:
    explicit SpikeWindow(dgl::Application& app) : Window(app) {}
    NoteCanvas* canvas = nullptr;
    bool        closedByUser = false;

protected:
    bool onClose() override {
        closedByUser = true;
        return true;
    }
    // Moved to a monitor with another scale factor (PMU's DGL patch calls this
    // on every configure event that changes it).
    void onScaleFactorChanged(double scaleFactor) override {
        if (canvas != nullptr) canvas->layer().rescale(scaleFactor);
    }
};

} // namespace

int main(int argc, char** argv) {
    bool        logFrames = false;
    bool        vsync     = false;
    const char* testDir   = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--log-frames") == 0) {
            logFrames = true;
        } else if (std::strcmp(argv[i], "--vsync") == 0) {
            vsync = true;
        } else if (std::strcmp(argv[i], "--self-test") == 0 && i + 1 < argc) {
            testDir = argv[++i];
        } else {
            std::fprintf(stderr, "usage: %s [--log-frames] [--vsync] [--self-test DIR]\n", argv[0]);
            return 2;
        }
    }
    if (std::getenv("DISPLAY") == nullptr) {
        std::fputs("No display (DISPLAY isn't set).\n", stderr);
        return 1;
    }

    Model model;
    Probe probe;
    probe.logFrames = logFrames;

    dgl::Application app(true);
    SpikeWindow      window(app);
    std::unique_ptr<NoteCanvas> canvas;
    {
        // The NanoVG and ImGui contexts are made in the constructors: the
        // window's GL context must be current (StageHand's spike's lesson).
        const dgl::Window::ScopedGraphicsContext context(window);
        canvas = std::make_unique<NoteCanvas>(window, model, probe);
        if (!vsync && !disableVsyncForCurrentContext()) std::fputs("Couldn't turn vsync off.\n", stderr);
    }
    window.canvas = canvas.get();

    const double sf = window.getScaleFactor();
    const auto   px = [sf](double logical) { return static_cast<unsigned>(std::lround(logical * sf)); };
    window.setTitle("Umpire - UI spike");
    window.setResizable(true);
    window.setGeometryConstraints(px(640), px(400));
    window.setSize(px(1100), px(700));
    window.show();

    int status = 0;
    if (testDir != nullptr) {
        status = runSelfTest(app, window, *canvas, model, probe, testDir);
    } else {
        // Driven like StageHand's MainLoop: idle() about every 10 ms.
        while (!window.closedByUser && !app.isQuitting() && !canvas->layer().quitRequested) {
            app.idle();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // DGL's teardown order: widgets with the GL context current, then the window, then the app.
    window.canvas = nullptr;
    {
        const dgl::Window::ScopedGraphicsContext context(window);
        canvas.reset();
    }
    window.close();
    app.quit();
    return status;
}
