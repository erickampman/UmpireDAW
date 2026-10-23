#include "Ui/MainWindow.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "Application.hpp"
#include "Window.hpp"

#include "Ui/GridCanvas.h"
#include "Ui/Panels.h"
#include "Ui/SelfTest.h"

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

class Window final : public dgl::Window {
public:
    explicit Window(dgl::Application& app) : dgl::Window(app) {}
    GridCanvas* canvas       = nullptr;
    bool        closedByUser = false;

protected:
    bool onClose() override {
        closedByUser = true;
        return true;
    }
    // Moved to a monitor with another scale factor (PMU's DGL patch calls this
    // on every configure event that changes it).
    void onScaleFactorChanged(double) override {
        if (canvas != nullptr) canvas->panels().rescale();
    }
};

} // namespace

struct MainWindow::Impl {
    Impl(ViewModel& m, Options o) : model(m), options(o), window(app) {}

    ViewModel&                  model;
    Options                     options;
    Probe                       probe;
    UmpireApplication           app;
    Window                      window;
    std::unique_ptr<GridCanvas> canvas;
    double                      secondStart = 0;
    std::uint64_t               framesAtSecond = 0;
};

std::unique_ptr<MainWindow> MainWindow::open(ViewModel& model, Options options) {
#ifndef __APPLE__
    if (std::getenv("DISPLAY") == nullptr) {
        std::fputs("No display (DISPLAY isn't set).\n", stderr);
        return nullptr;
    }
#endif
    auto impl = std::make_unique<Impl>(model, options);
    {
        // The NanoVG and ImGui contexts are made in the constructors: the
        // window's GL context must be current (StageHand's spike's lesson).
        const dgl::Window::ScopedGraphicsContext context(impl->window);
        impl->canvas = std::make_unique<GridCanvas>(impl->window, model, impl->probe);
    }
    impl->window.canvas = impl->canvas.get();

    const double sf = impl->window.getScaleFactor();
    const auto   px = [sf](double logical) { return static_cast<unsigned>(std::lround(logical * sf)); };
    impl->window.setTitle("Umpire");
    impl->window.setResizable(true);
    impl->window.setGeometryConstraints(px(640), px(400));
    impl->window.setSize(px(1100), px(700));
    impl->window.show();
    return std::unique_ptr<MainWindow>(new MainWindow(std::move(impl)));
}

MainWindow::MainWindow(std::unique_ptr<Impl> i) : impl(std::move(i)) {}

MainWindow::~MainWindow() {
    // DGL's teardown order: widgets with the GL context current, then the
    // window, then the app.
    impl->window.canvas = nullptr;
    {
        const dgl::Window::ScopedGraphicsContext context(impl->window);
        impl->canvas.reset();
    }
    impl->window.close();
    impl->app.quit();
}

void MainWindow::idle() {
    impl->app.idle();
    const double now = impl->app.getTime();
    if (now - impl->secondStart >= 1.0) {
        const std::uint64_t frames  = impl->canvas->panels().framesDrawn();
        impl->probe.framesLastSecond = static_cast<int>(frames - impl->framesAtSecond);
        impl->framesAtSecond         = frames;
        impl->secondStart            = now;
        if (impl->options.logFrames && impl->probe.framesLastSecond > 0) {
            std::printf("frames in the last second: %d\n", impl->probe.framesLastSecond);
            std::fflush(stdout);
        }
    }
}

bool MainWindow::closed() const {
    return impl->window.closedByUser || impl->app.isQuitting() || impl->canvas->panels().quitRequested;
}

int MainWindow::selfTest(const std::string& dir) {
    return runSelfTest(impl->app, impl->window, *impl->canvas, impl->model, impl->probe, dir);
}

} // namespace umpire::ui
