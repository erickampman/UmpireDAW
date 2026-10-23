#include "Ui/MainWindow.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "Application.hpp"
#include "Window.hpp"

#include "Ui/GridCanvas.h"
#include "Ui/GridPanels.h"
#include "Ui/GridSelfTest.h"
#include "Ui/TrackCanvas.h"
#include "Ui/TrackPanels.h"
#include "Ui/TrackSelfTest.h"

namespace umpire::ui {

namespace dgl = DGL_NAMESPACE;

namespace {

// DGL's Application, its windows' X11 class Umpire (WM_CLASS; DGL's own is its
// namespace). Set before any window is created: DGL realizes a window in its
// constructor.
class UmpireApplication final : public dgl::Application {
public:
    UmpireApplication() : Application(true) {
#ifndef __APPLE__
        setClassName("Umpire");
#endif
    }
};

// A window that remembers being closed by the user, and passes a change of
// scale factor (another monitor; PMU's DGL patch) to its ImGui panels.
class Window final : public dgl::Window {
public:
    explicit Window(dgl::Application& app) : dgl::Window(app) {}
    ImGuiWidget* panels       = nullptr;
    bool         closedByUser = false;

protected:
    bool onClose() override {
        closedByUser = true;
        return true;
    }
    void onScaleFactorChanged(double) override {
        if (panels != nullptr) panels->rescale();
    }
};

// A widget is created and destroyed with its window's GL context current (the
// NanoVG and ImGui contexts live in it: the spike's lesson), then the window closes.
template <typename Widget>
void destroyWith(dgl::Window& window, std::unique_ptr<Widget>& widget) {
    {
        const dgl::Window::ScopedGraphicsContext context(window);
        widget.reset();
    }
    window.close();
}

void place(dgl::Window& window, const char* title, double w, double h) {
    const double sf = window.getScaleFactor();
    const auto   px = [sf](double logical) { return static_cast<unsigned>(std::lround(logical * sf)); };
    window.setTitle(title);
    window.setResizable(true);
    window.setGeometryConstraints(px(640), px(400));
    window.setSize(px(w), px(h));
}

} // namespace

// The test grid window: milestone 4's grid over its own model, until
// milestone 6 opens a clip's grid this way.
struct GridWindow {
    explicit GridWindow(dgl::Application& app) : window(app) {
        {
            const dgl::Window::ScopedGraphicsContext context(window);
            canvas = std::make_unique<GridCanvas>(window, model, probe);
        }
        window.panels = &canvas->panels();
        place(window, "Umpire - test grid", 1000, 640);
        window.show();
    }
    ~GridWindow() {
        window.panels = nullptr;
        destroyWith(window, canvas);
    }
    bool closing() const { return window.closedByUser || canvas->panels().closeRequested; }

    GridModel                   model;
    Probe                       probe;
    Window                      window;
    std::unique_ptr<GridCanvas> canvas;
};

struct MainWindow::Impl {
    Impl(TrackController& c, Options o) : controller(c), options(o), window(app) {}

    TrackController&             controller;
    Options                      options;
    Probe                        probe;
    UmpireApplication            app;
    Window                       window;
    std::unique_ptr<TrackCanvas> canvas;
    std::unique_ptr<GridWindow>  grid;
    std::uint64_t                drawnVersion   = ~std::uint64_t{0};
    double                       secondStart    = 0;
    std::uint64_t                framesAtSecond = 0;
    bool                         quit           = false;
};

std::unique_ptr<MainWindow> MainWindow::open(TrackController& controller, Options options) {
#ifndef __APPLE__
    if (std::getenv("DISPLAY") == nullptr) {
        std::fputs("No display (DISPLAY isn't set).\n", stderr);
        return nullptr;
    }
#endif
    auto impl = std::make_unique<Impl>(controller, options);
    {
        const dgl::Window::ScopedGraphicsContext context(impl->window);
        impl->canvas = std::make_unique<TrackCanvas>(impl->window, controller, impl->probe);
    }
    impl->window.panels = &impl->canvas->panels();
    place(impl->window, "Umpire", 1200, 760);
    impl->window.show();
    return std::unique_ptr<MainWindow>(new MainWindow(std::move(impl)));
}

MainWindow::MainWindow(std::unique_ptr<Impl> i) : impl(std::move(i)) {}

MainWindow::~MainWindow() {
    // DGL's teardown order: other windows first, then the main window's
    // widgets with its GL context current, the window, then the app.
    impl->grid.reset();
    impl->window.panels = nullptr;
    destroyWith(impl->window, impl->canvas);
    impl->app.quit();
}

void MainWindow::idle() {
    Impl& m = *impl;
    // What the controller changed since the last frame.
    if (m.controller.version() != m.drawnVersion) {
        m.drawnVersion = m.controller.version();
        m.canvas->refresh();
    }
    m.app.idle();

    // The test grid window: opened from the View menu, torn down here, outside
    // DGL's event handling (a click in it can close it).
    TrackPanels& panels = m.canvas->panels();
    if (panels.gridRequested) {
        panels.gridRequested = false;
        if (!m.grid) m.grid = std::make_unique<GridWindow>(m.app);
        else m.grid->window.focus();
    }
    if (m.grid && m.grid->canvas->panels().quitRequested) m.quit = true;
    if (m.grid && m.grid->closing()) m.grid.reset();

    const double now = m.app.getTime();
    if (now - m.secondStart >= 1.0) {
        const std::uint64_t frames = panels.framesDrawn();
        m.probe.framesLastSecond   = static_cast<int>(frames - m.framesAtSecond);
        m.framesAtSecond           = frames;
        m.secondStart              = now;
        if (m.options.logFrames && m.probe.framesLastSecond > 0) {
            std::printf("frames in the last second: %d\n", m.probe.framesLastSecond);
            std::fflush(stdout);
        }
    }
}

bool MainWindow::closed() const {
    return impl->quit || impl->window.closedByUser || impl->canvas->panels().quitRequested;
}

int MainWindow::selfTest(const std::string& dir) {
    Impl& m = *impl;
    const auto start = std::chrono::steady_clock::now();
    // The app's loop: the controller's tick, then the windows'.
    const auto pump = [&] {
        m.controller.tick(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        idle();
    };
    int status = runTrackSelfTest(pump, m.window, *m.canvas, m.controller, m.probe, dir);

    std::puts("--- the test grid window");
    m.canvas->panels().gridRequested = true;
    for (int i = 0; i < 10; ++i) pump();
    if (!m.grid) {
        std::puts("FAIL  View > Test grid window opens it");
        return 1;
    }
    std::puts("PASS  View > Test grid window opens it");
    status |= runGridSelfTest(m.app, m.grid->window, *m.grid->canvas, m.grid->model, m.grid->probe, dir);
    m.grid->canvas->panels().closeRequested = true;
    pump();
    const bool gone = !m.grid;
    std::printf("%s  File > Close window closes it\n", gone ? "PASS" : "FAIL");
    return gone ? status : 1;
}

} // namespace umpire::ui
