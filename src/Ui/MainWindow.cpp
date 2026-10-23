#include "Ui/MainWindow.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <utility>

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

// A clip's grid window: milestone 4's grid, showing the clip's notes. Edits
// stay in the window until milestone 10 makes the grid edit the clip.
struct GridWindow {
    GridWindow(dgl::Application& app, const std::shared_ptr<const Clip>& clip) : window(app) {
        model.showClip(clip);
        {
            const dgl::Window::ScopedGraphicsContext context(window);
            canvas = std::make_unique<GridCanvas>(window, model, probe);
        }
        window.panels = &canvas->panels();
        place(window, ("Umpire - " + clip->name).c_str(), 1000, 640);
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
    std::map<ClipId, std::unique_ptr<GridWindow>> grids;   // by clip: one window each
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
    impl->grids.clear();
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

    // A clip double-clicked opens its grid, or brings it forward. Grid windows
    // are torn down here, outside DGL's event handling (a click in one can
    // close it).
    TrackPanels& panels = m.canvas->panels();
    if (const ClipRefId ref = std::exchange(m.canvas->openRequested, 0); ref != 0) {
        if (const auto clip = m.controller.clip(ref)) {
            auto& grid = m.grids[clip->id];
            if (!grid) grid = std::make_unique<GridWindow>(m.app, clip);
            else grid->window.focus();
        }
    }
    for (auto it = m.grids.begin(); it != m.grids.end();) {
        if (it->second->canvas->panels().quitRequested) m.quit = true;
        if (it->second->closing()) it = m.grids.erase(it);
        else ++it;
    }

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
    // The app's loop: the controller's tick, then the windows'.
    const auto pump = [&] {
        m.controller.tick();
        idle();
    };
    int status = runTrackSelfTest(pump, m.window, *m.canvas, m.controller, m.probe, dir);

    // The track view's test leaves a recorded clip selected; a double click opens it.
    std::puts("--- a clip's grid window");
    const ClipRefId ref = m.controller.selectedClip();
    const auto      clip = m.controller.clip(ref);
    if (!clip) {
        std::puts("FAIL  the track view's test recorded a clip");
        return 1;
    }
    m.canvas->openRequested = ref;
    for (int i = 0; i < 10; ++i) pump();
    const auto found = m.grids.find(clip->id);
    if (found == m.grids.end()) {
        std::puts("FAIL  a double click on a clip opens its grid");
        return 1;
    }
    GridWindow& grid = *found->second;
    const bool shows = grid.model.clipName == clip->name && !grid.model.notes.empty() &&
                       grid.model.notes.size() == notesOf(clip->events, clip->length).size();
    std::printf("%s  the clip's grid shows its notes: %zu\n", shows ? "PASS" : "FAIL", grid.model.notes.size());
    if (!shows) status = 1;
    // The grid's own checks start from an empty grid: they're about input, not the clip.
    grid.model.clear();
    grid.model.clipName = "Untitled clip";
    status |= runGridSelfTest(m.app, grid.window, *grid.canvas, grid.model, grid.probe, dir);
    grid.canvas->panels().closeRequested = true;
    pump();
    const bool gone = m.grids.empty();
    std::printf("%s  File > Close window closes it\n", gone ? "PASS" : "FAIL");
    return gone ? status : 1;
}

} // namespace umpire::ui
