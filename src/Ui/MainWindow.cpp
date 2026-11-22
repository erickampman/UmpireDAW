#include "Ui/MainWindow.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <thread>
#include <utility>

#include "Application.hpp"
#include "Window.hpp"

#include "App/FileDialog.h"
#include "App/SessionFile.h"
#include "Ui/GridCanvas.h"
#include "Ui/GridPanels.h"
#include "Ui/GridSelfTest.h"
#include "Ui/TrackCanvas.h"
#include "Ui/TrackPanels.h"
#include "Ui/TrackSelfTest.h"
#ifndef __APPLE__
#include "Ui/WindowIcon.h"
#endif
#include "Ui/Zoom.h"

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
    // The main window's: closing it is a Quit, which asks first if there's
    // something unsaved, so the window stays until the app quits.
    bool         askFirst     = false;

protected:
    bool onClose() override {
        closedByUser = true;
        return !askFirst;
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
#ifndef __APPLE__
    setWindowIcon(window.getNativeWindowHandle());   // the dock's and the window switcher's
#endif
}

} // namespace

// A clip's grid window (milestone 10, docs/clip-editor.md): the clip's notes
// in its scale, edited through the controller's commands, so undo covers them
// and every reference to the clip sees them. It follows the clip through
// edits, which make new clips, and undo.
struct GridWindow {
    // `zoom`: the session's grid zoom, which a new window opens at.
    GridWindow(dgl::Application& app, TrackController& controller, ClipRefId r, const std::shared_ptr<const Clip>& clip,
               float zoom, bool* hearNotes)
        : ref(r), seen(clip), window(app) {
        model.showClip(clip);
        model.chooseScale = [&controller, r](int edo, int reference) { controller.setClipScale(r, Scale{edo, reference}); };
        model.editClip    = [this, &controller](const std::string& label, const std::vector<NoteChange>& changes,
                                             const std::vector<NewNote>& added) {
            EditedClip made = controller.editNotes(ref, label, changes, added);
            if (made.clip) seen = made.clip;   // the model shows it itself
            return made;
        };
        model.renameClip = [&controller, r](const std::string& name) { controller.renameClip(r, name); };
        model.undo       = [&controller] { controller.undo(); };
        model.redo       = [&controller] { controller.redo(); };
        // HE: a note pressed sounds through the clip's track's output.
        model.hear       = [&controller, r](const uw::MidiMessage* messages, std::size_t count) {
            controller.hear(r, messages, count);
        };
        model.hearSwitch = hearNotes;
        {
            const dgl::Window::ScopedGraphicsContext context(window);
            canvas = std::make_unique<GridCanvas>(window, model, probe);
        }
        canvas->setZoom(zoom);
        zoomSeen      = canvas->zoom();
        window.panels = &canvas->panels();
        place(window, ("Umpire - " + clip->name).c_str(), 1000, 640);
        window.show();
    }
    ~GridWindow() {
        model.stopHearing();   // HE: closing mid-press leaves nothing sounding
        window.panels = nullptr;
        destroyWith(window, canvas);
    }
    bool closing() const { return window.closedByUser || canvas->panels().closeRequested; }

    // The clip as the controller has it now: an edit, a new scale or name
    // (or an undo) makes a new clip, which the window follows.
    void follow(const std::shared_ptr<const Clip>& clip, const TrackViewModel& view) {
        if (model.undoLabel != view.undoLabel || model.redoLabel != view.redoLabel) {
            model.undoLabel = view.undoLabel;
            model.redoLabel = view.redoLabel;
            canvas->panels().wake();
        }
        if (!clip || clip == seen) return;
        seen = clip;
        if (model.showingClip()) {
            model.showClip(clip);
        } else {   // edited: the notes keep their pitch in the new scale
            model.setTuning(clip->scale.edo, clip->scale.referenceNote);
            model.scaleFit        = clip->scaleFit;
            model.scaleCandidates = clip->scaleCandidates;
            model.touch();
        }
        canvas->panels().wake();
        canvas->repaint();
    }

    ClipRefId                   ref;
    std::shared_ptr<const Clip> seen;
    float                       zoomSeen = 1;   // the canvas's zoom, as idle() last saw it
    GridModel                   model;
    Probe                       probe;
    Window                      window;
    std::unique_ptr<GridCanvas> canvas;
};

struct MainWindow::Impl {
    Impl(TrackController& c, Options o) : controller(c), options(o), window(app) {}

    using FileAction = TrackPanels::FileAction;

    // --- File (milestone 9, docs/files.md) ---
    // An action from the menu, the keys or closing the window: New, Open and
    // Quit ask to save first if anything is unsaved.
    void fileAction(FileAction action);
    // The action an unsaved session was asked about, once it's settled.
    void proceed(FileAction action);
    void save(FileAction then);
    void saveAs(FileAction then);
    void saveTo(const std::string& bundle, FileAction then);
    void chose(const std::string& path);   // the file dialog closed
    void answered(int button);             // the prompt's
    void ask(std::string text, std::vector<std::string> buttons);
    void tell(const std::string& text) { ask(text, {"OK"}); }
    std::string startFolder() const;
    void        updateTitle();

    enum class Asking { Nothing, SaveChanges, Replace, Told };
    FileDialog  dialog;
    FileAction  dialogFor = FileAction::None;   // Open, or SaveAs
    FileAction  afterSave = FileAction::None;   // what a save makes way for
    Asking      asking    = Asking::Nothing;
    std::string replacing;                      // the bundle Replace asks about
    std::string lastFolder;
    std::string shownTitle;

    TrackController&             controller;
    Options                      options;
    Probe                        probe;
    UmpireApplication            app;
    Window                       window;
    std::unique_ptr<TrackCanvas> canvas;
    std::vector<std::unique_ptr<GridWindow>> grids;   // one window per clip
    // The window showing `clip`, or null.
    GridWindow* gridFor(const std::shared_ptr<const Clip>& clip) {
        for (auto& g : grids)
            if (g->seen == clip) return g.get();
        return nullptr;
    }
    // How the session is being looked at (SessionFile.h): the track view's
    // zoom is its canvas's; the grids' is the last zoom any grid window was
    // left at, which the next one opens at. Each grid window zooms on its own.
    float                        gridZoom   = 1;
    std::uint64_t                viewOpened = ~std::uint64_t{0};   // controller.opened() when last taken
    bool                         hearNotes  = true;   // HE: every grid window's Edit > Hear notes while editing
    void                         keepView() { controller.keepView({canvas->timeZoom(), gridZoom, hearNotes}); }
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
    impl->window.panels   = &impl->canvas->panels();
    impl->window.askFirst = true;
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
    // Another session opened or made (here, or at launch): as it was left.
    if (m.controller.opened() != m.viewOpened) {
        m.viewOpened = m.controller.opened();
        // Only if it's another zoom: setting it draws again.
        if (const float zoom = Zoom::clamp(m.controller.keptView().trackZoom); zoom != m.canvas->timeZoom())
            m.canvas->setZoom(zoom);
        m.gridZoom  = Zoom::clamp(m.controller.keptView().gridZoom);
        m.hearNotes = m.controller.keptView().hearNotes;
    }
    m.app.idle();

    // A clip double-clicked opens its grid, or brings it forward. Grid windows
    // are torn down here, outside DGL's event handling (a click in one can
    // close it).
    TrackPanels& panels = m.canvas->panels();
    if (const ClipRefId ref = std::exchange(m.canvas->openRequested, 0); ref != 0) {
        if (const auto clip = m.controller.clip(ref)) {
            if (GridWindow* grid = m.gridFor(clip)) grid->window.focus();
            else m.grids.push_back(std::make_unique<GridWindow>(m.app, m.controller, ref, clip, m.gridZoom, &m.hearNotes));
        }
    }
    for (auto& grid : m.grids) {
        grid->follow(m.controller.clip(grid->ref), m.controller.view());
        if (grid->canvas->zoom() != grid->zoomSeen) m.gridZoom = grid->zoomSeen = grid->canvas->zoom();
    }
    for (auto it = m.grids.begin(); it != m.grids.end();) {
        if (std::exchange((*it)->canvas->panels().quitRequested, false)) m.fileAction(Impl::FileAction::Quit);
        if ((*it)->closing()) it = m.grids.erase(it);
        else ++it;
    }

    // File: the window's close button is Quit; then the menu's and the keys'
    // actions, the prompt's answer and the file dialog's choice.
    if (std::exchange(m.window.closedByUser, false)) m.fileAction(Impl::FileAction::Quit);
    if (const auto action = std::exchange(panels.fileRequested, Impl::FileAction::None); action != Impl::FileAction::None)
        m.fileAction(action);
    if (const int answer = std::exchange(panels.promptAnswer, -1); answer >= 0) m.answered(answer);
    if (std::string chosen; m.dialog.poll(chosen)) m.chose(chosen);
    m.updateTitle();

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

bool MainWindow::closed() const { return impl->quit; }

// --- File -------------------------------------------------------------------------------

void MainWindow::Impl::fileAction(FileAction action) {
    // One at a time: a dialog or a question already up waits for its answer.
    if (dialog.running() || canvas->panels().prompt) return;
    if (action == FileAction::Save) return save(FileAction::None);
    if (action == FileAction::SaveAs) return saveAs(FileAction::None);
    if (!controller.modified()) return proceed(action);
    afterSave = action;
    asking    = Asking::SaveChanges;
    ask("Save changes to \"" + controller.view().sessionName + "\" first?", {"Save", "Don't save", "Cancel"});
}

void MainWindow::Impl::proceed(FileAction action) {
    afterSave = FileAction::None;
    switch (action) {
        case FileAction::New:
            grids.clear();   // their clips go with the session
            controller.newSession();
            break;
        case FileAction::Open: {
            std::string problem;
            if (dialog.start(FileDialog::Mode::OpenFolder, "Open a session (a .umpire folder)", startFolder(), "", problem))
                dialogFor = FileAction::Open;
            else
                tell("Can't show a file dialog: " + problem + ".");
            break;
        }
        case FileAction::Quit: quit = true; break;
        default: break;
    }
}

void MainWindow::Impl::save(FileAction then) {
    if (controller.path().empty()) return saveAs(then);
    saveTo(controller.path(), then);
}

void MainWindow::Impl::saveAs(FileAction then) {
    std::string problem;
    const std::string name = controller.view().sessionName + kSessionExtension;
    if (!dialog.start(FileDialog::Mode::Save, "Save the session as", startFolder(), name, problem))
        return tell("Can't show a file dialog: " + problem + ".");
    dialogFor = FileAction::SaveAs;
    afterSave = then;
}

void MainWindow::Impl::saveTo(const std::string& bundle, FileAction then) {
    keepView();
    const SessionFileResult result = controller.save(bundle);
    if (!result.ok) {
        afterSave = FileAction::None;
        return tell("Couldn't save " + bundle + ": " + result.error);
    }
    lastFolder = std::filesystem::path(bundle).parent_path().string();
    if (then != FileAction::None) proceed(then);
}

void MainWindow::Impl::chose(const std::string& path) {
    const FileAction what = std::exchange(dialogFor, FileAction::None);
    if (path.empty()) {   // cancelled: whatever was waiting on it is too
        afterSave = FileAction::None;
        return;
    }
    if (what == FileAction::Open) {
        const std::string bundle = sessionBundleOf(path);
        if (bundle.empty()) return tell(path + " isn't an Umpire session (a .umpire folder holding session.json).");
        const SessionFileResult result = controller.open(bundle);
        if (!result.ok) return tell("Couldn't open " + bundle + ": " + result.error);
        grids.clear();
        lastFolder = std::filesystem::path(bundle).parent_path().string();
        return;
    }
    // Save As: the bundle, checked before anything is replaced.
    const std::string bundle = saveAsPath(path);
    switch (classifySaveTarget(bundle, controller.path())) {
        case SaveTarget::New:
        case SaveTarget::Current: return saveTo(bundle, afterSave);
        case SaveTarget::OtherSession:
            replacing = bundle;
            asking    = Asking::Replace;
            return ask("There's already a session called \"" + sessionNameOf(bundle) + "\" there. Replace it?",
                       {"Replace", "Cancel"});
        case SaveTarget::NotASession:
            afterSave = FileAction::None;
            return tell(bundle + " is already there and isn't an Umpire session, so it won't be replaced.");
    }
}

void MainWindow::Impl::answered(int button) {
    const Asking was = std::exchange(asking, Asking::Nothing);
    if (was == Asking::SaveChanges) {
        if (button == 0) save(afterSave);
        else if (button == 1) proceed(afterSave);
        else afterSave = FileAction::None;
    } else if (was == Asking::Replace) {
        if (button == 0) saveTo(replacing, afterSave);
        else afterSave = FileAction::None;
    }
}

void MainWindow::Impl::ask(std::string text, std::vector<std::string> buttons) {
    if (buttons.size() == 1 && asking == Asking::Nothing) asking = Asking::Told;
    canvas->panels().prompt = TrackPanels::Prompt{std::move(text), std::move(buttons)};
    canvas->panels().wake();
}

// Where the dialogs start: beside the session, else where the last one was
// chosen, else home.
std::string MainWindow::Impl::startFolder() const {
    if (!controller.path().empty()) return std::filesystem::path(controller.path()).parent_path().string();
    if (!lastFolder.empty()) return lastFolder;
    const char* home = std::getenv("HOME");
    return home != nullptr ? home : ".";
}

// "Song • — Umpire" while there's something unsaved.
void MainWindow::Impl::updateTitle() {
    const TrackViewModel& view  = controller.view();
    const std::string     title = view.sessionName + (view.modified ? " \u2022" : "") + " \u2014 Umpire";
    if (title == shownTitle) return;
    shownTitle = title;
    window.setTitle(title.c_str());
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
    GridWindow* const found = m.gridFor(clip);
    if (found == nullptr) {
        std::puts("FAIL  a double click on a clip opens its grid");
        return 1;
    }
    GridWindow& grid = *found;
    const bool shows = grid.model.clipName == clip->name && !grid.model.notes.empty() &&
                       grid.model.notes.size() == notesOf(clip->events, clip->length).size();
    std::printf("%s  the clip's grid shows its notes: %zu\n", shows ? "PASS" : "FAIL", grid.model.notes.size());
    if (!shows) status = 1;
    const auto check = [&status](const char* what, bool ok) {
        std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok) status = 1;
    };
    check("it opens in the clip's scale, worked out",
          grid.model.edo == clip->scale.edo && grid.model.referenceNote == clip->scale.referenceNote &&
              grid.model.scaleFit == ScaleFit::WorkedOut);
    grid.model.chooseScale(31, 57);   // as the panel does when a slider is let go
    for (int i = 0; i < 5; ++i) pump();
    check("a scale chosen in the grid is the clip's, and the grid follows it",
          m.controller.clip(ref)->scale == (Scale{31, 57}) && grid.model.edo == 31 && grid.model.referenceNote == 57 &&
              grid.model.scaleFit == ScaleFit::Chosen && m.controller.view().undoLabel == "Set scale");
    const auto settle = [&](int ms) {   // time for the window to draw
        for (int i = 0; i < ms / 5; ++i) {
            pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    grid.model.select(1);   // the inspector shows what was sent
    settle(300);   // a new window's first frames are ImGui laying out
    grid.canvas->panels().captureNextFrame(dir + "/g00-31edo.ppm");
    grid.canvas->panels().wake(1);
    for (int i = 0; i < 40; ++i) {
        pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::printf("      screenshot %s/g00-31edo.ppm\n", dir.c_str());
    m.controller.undo();
    for (int i = 0; i < 5; ++i) pump();
    check("undo, and the grid follows back", grid.model.edo == clip->scale.edo && grid.model.scaleFit == ScaleFit::WorkedOut);
    // The grid's own checks start from the clip emptied: they're about input,
    // and editing the clip through commands.
    grid.model.clear();
    grid.model.commit();
    status |= runGridSelfTest(pump, grid.window, *grid.canvas, grid.model, grid.probe, dir);
    grid.canvas->panels().closeRequested = true;
    pump();
    const bool gone = m.grids.empty();
    std::printf("%s  File > Close window closes it\n", gone ? "PASS" : "FAIL");
    if (!gone) return 1;

    // The zoom a session keeps (docs/files.md): saved, then opened again,
    // the track view and the next grid window are as they were left.
    std::puts("--- the zoom a session keeps");
    m.canvas->openRequested = ref;
    for (int i = 0; i < 5; ++i) pump();
    if (!m.grids.empty()) m.grids.front()->canvas->setZoom(0.5f);
    m.canvas->setZoom(2);
    for (int i = 0; i < 5; ++i) pump();
    check("the grid window's zoom is the one the next opens at", m.gridZoom == 0.5f);
    const std::string bundle = dir + "/Zoom.umpire";
    m.saveTo(bundle, Impl::FileAction::None);
    check("zooming then saving leaves nothing unsaved", !m.controller.modified());
    m.grids.clear();
    m.canvas->setZoom(1);
    m.gridZoom = 1;
    const SessionFileResult opened = m.controller.open(bundle);
    for (int i = 0; i < 5; ++i) pump();
    check("opened again, the track view is at its zoom", opened.ok && m.canvas->timeZoom() == 2);
    m.canvas->openRequested = m.controller.tracks().empty() || m.controller.tracks()[0].clips.empty()
                                  ? 0
                                  : m.controller.tracks()[0].clips[0].id;
    for (int i = 0; i < 5; ++i) pump();
    check("and a grid window opens at the grid's", !m.grids.empty() && m.grids.front()->canvas->zoom() == 0.5f);
    m.grids.clear();
    m.controller.newSession();
    for (int i = 0; i < 5; ++i) pump();
    check("New: zoom 1", m.canvas->timeZoom() == 1 && m.gridZoom == 1);
    std::filesystem::remove_all(bundle);
    return status;
}

} // namespace umpire::ui
