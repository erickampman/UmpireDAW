// umpire-daw: Umpire, a DAW built on MIDI 2.0's Universal MIDI Packet.
// Milestone 7 (docs/playback.md): the track view, fed by the platform's MIDI
// input, with a transport that records it into clips and plays them to the
// platform's MIDI outputs; driven by the main loop.
//
//   umpire-daw                   the window
//   umpire-daw --log-frames      ... printing frames drawn per second
//   umpire-daw --self-test DIR   scripted input, checks, screenshots in DIR

#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "App/MainLoop.h"
#include "App/MidiInputs.h"
#include "App/TrackController.h"
#include "Ui/MainWindow.h"
#include "uw/midi/PlatformMidiInput.h"
#include "uw/midi/PlatformMidiOutput.h"

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

    // MIDI input: ALSA sequencer UMP (Linux) or CoreMIDI (macOS). Without it,
    // the track view still opens; nothing arrives.
    std::unique_ptr<uw::PlatformMidiInput> platform = uw::PlatformMidiInput::create("Umpire");
    umpire::MidiInputs                     inputs(platform.get());
    inputs.openAll();
    // MIDI output, as its own client ("Umpire Out"), not offering Umpire's
    // inputs: a track playing into them would loop.
    std::unique_ptr<uw::PlatformMidiOutput> platformOut = uw::PlatformMidiOutput::create("Umpire Out");
    umpire::MidiOutputs outputs(platformOut.get(), {"Umpire", "Umpire MIDI 2.0",   // ALSA's clients; CoreMIDI's names
                                                    std::string("Umpire ") + umpire::MidiInputs::kVirtualMidi2,
                                                    std::string("Umpire ") + umpire::MidiInputs::kVirtualMidi1});
    umpire::TrackController controller(inputs, outputs);

    auto window = umpire::ui::MainWindow::open(controller, options);
    if (!window) return 1;
    if (testDir != nullptr) return window->selfTest(testDir);

    umpire::MainLoop loop;
    signalLoop = &loop;
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    // The controller's tick, then the windows' events and drawing, as StageHand
    // drives its rack window.
    loop.addTimer(10, [&] {
        controller.tick();
        window->idle();
        if (window->closed()) loop.requestQuit();
    });
    loop.run();
    signalLoop = nullptr;
    return 0;
}
