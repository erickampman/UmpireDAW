// umpire-daw: Umpire, a DAW built on MIDI 2.0's Universal MIDI Packet.
// Milestones 7-8 (docs/playback.md, docs/plugins.md): the track view, fed by
// the platform's MIDI input, with a transport that records it into clips and
// plays them to the platform's MIDI outputs or to hosted CLAP instruments;
// driven by the main loop.
//
//   umpire-daw                   the window, with a new session
//   umpire-daw SESSION           ... with a saved one open (X.umpire; milestone 9, docs/files.md)
//   umpire-daw --log-frames      ... printing frames drawn per second
//   umpire-daw --self-test DIR   scripted input, checks, screenshots in DIR

#include <csignal>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#ifndef __APPLE__
#include <unistd.h>
#endif

#include "App/AudioEngine.h"
#include "App/MainLoop.h"
#include "App/MidiInputs.h"
#include "App/PlatformAudioBackend.h"
#include "App/SessionFile.h"
#include "App/TrackController.h"
#include "Ui/MainWindow.h"
#include "uw/clap/ClapPluginFactory.h"
#include "uw/midi/PlatformMidiInput.h"
#include "uw/midi/PlatformMidiOutput.h"

namespace {

umpire::MainLoop* signalLoop = nullptr;

void onSignal(int) {
    if (signalLoop != nullptr) signalLoop->requestQuit();
}

#ifndef __APPLE__
// Started without pw-jack (from the launcher, say) and no JACK server in reach:
// run again under pw-jack, once, as StageHand does (its docs/command-line.md,
// "Audio: pw-jack"). The ordinary libjack is jackd2's, which reaches only a
// real jackd; PipeWire's JACK layer needs pw-jack. Without this, every plugin
// output plays into silence. Returns if a restart isn't needed or didn't work.
void restartUnderPwJackIfNeeded(int argc, char** argv) {
    constexpr const char* kMarker = "UMPIRE_PW_JACK";
    if (std::getenv(kMarker) != nullptr || uw::JackBackend::serverAvailable()) return;

    std::error_code    ec;
    const std::string  self = std::filesystem::read_symlink("/proc/self/exe", ec).string();
    std::vector<char*> args{const_cast<char*>("pw-jack"), ec ? argv[0] : const_cast<char*>(self.c_str())};
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
    args.push_back(nullptr);

    std::puts("No JACK server in reach; restarting under pw-jack.");
    std::fflush(stdout);
    setenv(kMarker, "1", 1);
    execvp("pw-jack", args.data());   // returns only on failure
    std::fprintf(stderr, "Couldn't run pw-jack: %s\n", std::strerror(errno));
    unsetenv(kMarker);
}
#endif

} // namespace

int main(int argc, char** argv) {
    umpire::ui::MainWindow::Options options;
    const char*                     testDir = nullptr;
    const char*                     session = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--log-frames") == 0) {
            options.logFrames = true;
        } else if (std::strcmp(argv[i], "--self-test") == 0 && i + 1 < argc) {
            testDir = argv[++i];
        } else if (argv[i][0] != '-' && session == nullptr) {
            session = argv[i];
        } else {
            std::fprintf(stderr, "usage: %s [--log-frames] [--self-test DIR] [SESSION.umpire]\n", argv[0]);
            return 2;
        }
    }

#ifndef __APPLE__
    // Before anything opens (MIDI clients, the window): exec replaces the
    // process. Not for --self-test, which never opens audio.
    if (testDir == nullptr) restartUnderPwJackIfNeeded(argc, argv);
#endif

    // The main loop: plugin editors (milestone 11) need it from the moment a
    // voice can open one, so it's made before AudioEngine, not just before
    // run().
    umpire::MainLoop loop;
    signalLoop = &loop;
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

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
    // CLAP instruments as track outputs (milestone 8): JACK on Linux,
    // CoreAudio on macOS (App/PlatformAudioBackend.h). Without a backend,
    // or if it won't open, a track can still be given a plugin output; it
    // just plays into silence.
    uw::ClapPluginFactory              pluginFactory;
    std::unique_ptr<uw::AudioBackend>  audioBackend = umpire::createAudioBackend("Umpire");
    umpire::AudioEngine                engine(audioBackend.get(), pluginFactory, loop);

    umpire::TrackController controller(inputs, outputs, engine);

    auto window = umpire::ui::MainWindow::open(controller, options);
    if (!window) return 1;
    if (testDir != nullptr) return window->selfTest(testDir);   // never touches real audio or plugins

    // Scanning loads every CLAP plugin it finds to read its descriptors
    // (App/Instruments.h), so it happens here, not during --self-test or a
    // ctest suite.
    engine.instruments().rescan();
    // Best-effort: silence, not a hard failure, if it can't; but said, since
    // every plugin output is then silent.
    if (!engine.start()) std::fputs("No audio output: CLAP instruments will be silent.\n", stderr);
    // After the scan, so its plugin outputs find their plugins.
    if (session != nullptr) {
        const std::string bundle = umpire::sessionBundleOf(session);
        const umpire::SessionFileResult opened =
            bundle.empty() ? umpire::SessionFileResult{false, "not an Umpire session"} : controller.open(bundle);
        if (!opened.ok) std::fprintf(stderr, "Couldn't open %s: %s\n", session, opened.error.c_str());
    }

    // The controller's tick, then the windows' events and drawing, as StageHand
    // drives its rack window.
    loop.addTimer(10, [&] {
        controller.tick();
        window->idle();
        if (window->closed()) loop.requestQuit();
    });
    loop.run();
    signalLoop = nullptr;
    engine.stop();
    return 0;
}
