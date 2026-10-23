// umpire-daw: Umpire, a DAW built on MIDI 2.0's Universal MIDI Packet.
// Milestone 4's skeleton (docs/skeleton.md): one main window, the NanoVG
// canvas under the ImGui panels, over the view model, driven by the main loop.
//
//   umpire-daw                   the window
//   umpire-daw --log-frames      ... printing frames drawn per second
//   umpire-daw --self-test DIR   scripted input, checks, screenshots in DIR

#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>

#include "App/MainLoop.h"
#include "App/ViewModel.h"
#include "Ui/MainWindow.h"

namespace {

umpire::MainLoop* signalLoop = nullptr;

void onSignal(int) {
    if (signalLoop != nullptr) signalLoop->requestQuit();
}

} // namespace

int main(int argc, char** argv) {
    umpire::ui::MainWindow::Options options;
    const char*                     testDir = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--log-frames") == 0) {
            options.logFrames = true;
        } else if (std::strcmp(argv[i], "--self-test") == 0 && i + 1 < argc) {
            testDir = argv[++i];
        } else {
            std::fprintf(stderr, "usage: %s [--log-frames] [--self-test DIR]\n", argv[0]);
            return 2;
        }
    }

    umpire::ViewModel model;
    auto              window = umpire::ui::MainWindow::open(model, options);
    if (!window) return 1;
    if (testDir != nullptr) return window->selfTest(testDir);

    umpire::MainLoop loop;
    signalLoop = &loop;
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    // The window's events and drawing, as StageHand drives its rack window.
    loop.addTimer(10, [&] {
        window->idle();
        if (window->closed()) loop.requestQuit();
    });
    loop.run();
    signalLoop = nullptr;
    return 0;
}
