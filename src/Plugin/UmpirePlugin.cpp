// Umpire as a CLAP instrument (docs/plugin-version.md: P1 the spike, P2
// recording and playback).
//
// Our own CLAP entry point, as Retune's (no DPF wrapper: DPF has no recording
// flag and doesn't pass on timer-support or posix-fd-support), wrapped as a
// VST3 by clap-wrapper for Ardour. The notes never pass through the host:
//   - the plugin opens one MIDI input itself, through uw-core (CoreMIDI on
//     the Mac, the ALSA sequencer on Linux), and takes no MIDI from the host;
//   - it hosts one CLAP synth through uw-core's CLAP host (uw::ClapPlugin)
//     and runs it inside its own process() (Plugin/PluginEngine);
//   - it records while the host records and its Record parameter (its arm)
//     is on, each take a clip on its own lane (Plugin/Lane, kept by the lane
//     thread, Plugin/LaneKeeper), and plays the lane back from the host's
//     position; the host hears only audio.
//
// Its window (P3, Plugin/PluginEditor.h) is its own, embedded in the host's
// plugin window: Record, the lane of takes in time with the host, and the
// grid for the selected take, whose edits go back through the lane. The
// synth's editor opens in a window of its own (uw::EditorWindow), from a
// button in Umpire's (docs/plugin-version.md, "P3"). What it does also goes
// to a log file (logPath() below): the input it opened, takes, the live
// input's timing, the state, its latency, and a heartbeat counting the
// host's timer ticks and main-thread callbacks (how the spike checked
// timer-support with the window closed, check 7). What a new instance opens
// comes from a small text file (configPath(), Plugin/PluginSetup.h).
//
// P4 (docs/plugin-version.md, "P4"): the window chooses the synth and the
// input per instance, and the state keeps them, with the takes' names and
// scales. A synth chosen while the plugin runs is swapped in by the audio
// thread (PluginEngine::swapSynth), and "Change synth" is in the plugin's
// undo with the old synth's state. A synth or input that isn't here is kept
// as it was, not replaced.

#include <clap/clap.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>   // the window's own timer, on a host without timer-support
#endif

#include "App/ClipFile.h"
#include "Plugin/LaneKeeper.h"
#include "Plugin/PluginEditor.h"
#include "Plugin/PluginEngine.h"
#include "Plugin/PluginSetup.h"
#include "uw/base/Clock.h"
#include "uw/clap/ClapPluginFactory.h"
#include "uw/midi/PlatformMidiInput.h"
#include "uw/plugin/EditorWindow.h"
#include "uw/plugin/HostServices.h"
#include "uw/ump/Ump.h"

#ifndef UMPIRE_VERSION_STRING
#define UMPIRE_VERSION_STRING "0.0.0"
#endif

namespace umpire::plugin {
namespace {

namespace fs = std::filesystem;

#ifdef __APPLE__
constexpr const char* kWindowApi = CLAP_WINDOW_API_COCOA;
#else
constexpr const char* kWindowApi = CLAP_WINDOW_API_X11;
#endif

// --- where things are -------------------------------------------------------------------

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr ? v : "";
}

fs::path home() { return env("HOME"); }

// UMPIRE_PLUGIN_CONF names another (the tests use it).
fs::path configPath() {
    if (const std::string over = env("UMPIRE_PLUGIN_CONF"); !over.empty()) return over;
#ifdef __APPLE__
    return home() / "Library/Application Support/Umpire/plugin.conf";
#else
    const std::string xdg = env("XDG_CONFIG_HOME");
    return (xdg.empty() ? home() / ".config" : fs::path(xdg)) / "umpire/plugin.conf";
#endif
}

// UMPIRE_PLUGIN_LOG names another; "none", no log.
fs::path logPath() {
    if (const std::string over = env("UMPIRE_PLUGIN_LOG"); !over.empty()) return over;
#ifdef __APPLE__
    return home() / "Library/Logs/Umpire/plugin.log";
#else
    const std::string xdg = env("XDG_STATE_HOME");
    return (xdg.empty() ? home() / ".local/state" : fs::path(xdg)) / "umpire/plugin.log";
#endif
}

std::mutex logMutex;   // instances may share a process and log from the host's threads

void writeLog(int instance, const std::string& line) {
    static const fs::path path = logPath();
    if (path == "none") return;
    std::lock_guard<std::mutex> lock(logMutex);
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::FILE* f = std::fopen(path.c_str(), "a");
    if (f == nullptr) return;
    const auto now   = std::chrono::system_clock::now();
    const auto t     = std::chrono::system_clock::to_time_t(now);
    const auto ms    = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm    local{};
    localtime_r(&t, &local);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &local);
    std::fprintf(f, "%s.%03lld [%d] %s\n", stamp, static_cast<long long>(ms), instance, line.c_str());
    std::fclose(f);
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// The .clap called `name` in CLAP's folders, not looking inside bundles; a
// path is taken as it is.
std::string findClap(const std::vector<std::string>& dirs, const std::string& name) {
    std::error_code ec;
    if (name.find('/') != std::string::npos) return fs::exists(name, ec) ? name : std::string{};
    const std::string file = name + ".clap";
    for (const std::string& dir : dirs)
        if (fs::exists(fs::path(dir) / file, ec)) return (fs::path(dir) / file).string();
    for (const std::string& dir : dirs) {
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (it->path().extension() != ".clap") continue;
            if (it->path().filename() == file) return it->path().string();
            it.disable_recursion_pending();
        }
    }
    return {};
}

std::atomic<int> nextInstance{1};

constexpr const char* kOwnId = "com.unlikelyware.umpire";

// Which synth an instance hosts (P4): the setup file's until one is chosen in
// the window, and the state keeps the choice. With its state: for undo, and
// for a synth that isn't installed here, which is kept as it was.
struct SynthChoice {
    bool                   chosen = false;   // false: the setup file's
    std::string            id, path, name;   // nothing named: none
    std::vector<std::byte> state;
};

// One factory for every instance in the process, so they share each loaded
// .clap (its ClapModule): two instances hosting PlainOldSynth load it once,
// and its clap_entry is deinitialized only when the last of them goes
// (check 6). Main thread only, as CLAP's init and destroy are.
std::shared_ptr<uw::ClapPluginFactory> sharedFactory() {
    static std::weak_ptr<uw::ClapPluginFactory> shared;
    std::shared_ptr<uw::ClapPluginFactory>      factory = shared.lock();
    if (!factory) {
        factory = std::make_shared<uw::ClapPluginFactory>();
        shared  = factory;
    }
    return factory;
}

class Plugin;

// What the hosted synth sees as its host (uw::HostServices): the main-loop
// services go on to the outer host's own CLAP timer-support and
// posix-fd-support, which is what a synth's editor on Linux runs on.
class ForwardingHost final : public uw::HostServices {
public:
    explicit ForwardingHost(Plugin& p) : plugin(p) {}
    void         requestRestart() override;
    void         requestProcess() override;
    void         requestParamFlush() override {}
    void         log(uw::LogLevel level, const char* message) override;
    uw::HostInfo hostInfo() const override;
    void         stateChanged() override;
    void         latencyChanged() override;
    uw::TimerId  addTimer(std::uint32_t periodMs, std::function<void()> callback) override;
    void         removeTimer(uw::TimerId id) override;
    bool watchFd(int fd, std::uint32_t events, std::function<void(std::uint32_t)> callback) override;
    bool modifyFd(int fd, std::uint32_t events) override;
    void unwatchFd(int fd) override;

private:
    Plugin& plugin;
};

class Plugin {
public:
    explicit Plugin(const clap_host_t* h);

    clap_plugin_t clap{};
    const clap_host_t* host;
    const clap_host_log_t*            hostLog   = nullptr;
    const clap_host_timer_support_t*  hostTimer = nullptr;
    const clap_host_posix_fd_support_t* hostFd  = nullptr;
    const clap_host_gui_t*            hostGui   = nullptr;
    const clap_host_state_t*          hostState = nullptr;
    const clap_host_latency_t*        hostLatency = nullptr;
    const clap_host_params_t*         hostParams  = nullptr;
    const int                         instance;

    Config         config;
    ForwardingHost services{*this};
    PluginEngine   engine;
    LaneKeeper     keeper{engine, [this](const ClipRef& take) { logTake(take); }};

    std::shared_ptr<uw::ClapPluginFactory> factory;
    std::unique_ptr<uw::HostedPlugin>      synth;
    // P4: the synth chosen in the window (or the state), and what it needs.
    std::string                            synthPath;     // the .clap it came from
    bool                                   synthChosen = false;
    SynthChoice                            missingSynth;  // id set: the chosen synth isn't here, kept as it was
    std::vector<std::unique_ptr<uw::HostedPlugin>> retiring;   // swapped out; the audio thread lets go of them

    std::unique_ptr<uw::PlatformMidiInput> midi;
    uw::MidiSourceHandle                   source = uw::kInvalidMidiSource;
    std::string                            sourceName;
    std::uint64_t                          seenChanges = 0;
    bool                                   sourceConnected = false;
    InputChoice                            inputChoice;   // P4: chosen in the window, kept in the state
    ViewZoom                               zoom;          // Z1: the window's, kept in the state; not dirtying
    bool                                   inputMissing = false;   // the source chosen isn't here

    // Main-loop services forwarded for the synth: the host's ids to ours.
    struct Timer {
        uw::TimerId           id = uw::kInvalidTimer;
        std::function<void()> callback;
    };
    std::map<clap_id, Timer>                        timers;
    uw::TimerId                                     nextTimer = 1;
    std::map<int, std::function<void(std::uint32_t)>> fds;
    clap_id                                         ownTimer = CLAP_INVALID_ID;

    // Diagnostics (main thread, except framesSinceCallback: audio thread).
    std::uint64_t timerTicks = 0, callbacks = 0, synthTimerTicks = 0;
    std::chrono::steady_clock::time_point lastReport = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point nextInputTry{};
    bool          wasRecording = false;
    bool          activating   = false;
    std::uint32_t latency      = 0;   // the synth's, reported as ours
    bool          activeOnce   = false;
    double        callbackFrames = 2400;   // a main-thread callback asked for every 50 ms of audio
    double        framesSinceCallback = 0;

    // Transport events and the arm's changes in the block (audio thread).
    std::array<TransportChange, 64> changes{};

    // The Record parameter (the arm): written by the audio thread (or by
    // params flush and the state, never at once), read by the main thread for
    // the state and the log.
    std::atomic<bool> armed{true};          // on in a new instance (plugin-version.md, "The arm")
    bool              loggedArmed = true;    // main thread
    // The host's transport flags at the last block's start, as the host gave them
    // (CLAP_TRANSPORT_*; ~0u for no transport), and the last logged.
    std::atomic<std::uint32_t> transportFlags{~0u};
    std::uint32_t              loggedTransportFlags = ~0u - 1;   // main thread

    // The Record button pressed in the window: 0 or 1 for the audio thread
    // (or params flush) to apply and tell the host, as a gesture; -1 none.
    std::atomic<int> guiArm{-1};

    // Umpire's window (P3), embedded in the host's, and the synth's editor in
    // a window of its own. Main thread.
    bool                              guiCreated = false;
    std::unique_ptr<PluginEditor>     window;
    std::uintptr_t                    guiParent = 0;
    std::uint32_t                     guiWidth = PluginEditor::kWidth, guiHeight = PluginEditor::kHeight;   // the host's units
    double                            guiScale = 1.0;   // pixels per unit: the Mac's backing scale; set_scale on X11
    clap_id                           guiTimer = CLAP_INVALID_ID;
#ifdef __APPLE__
    CFRunLoopTimerRef                 ownGuiTimer = nullptr;
#endif
    std::unique_ptr<uw::EditorWindow> synthWindow;

    bool armedForWindow() const {
        const int pressed = guiArm.load();
        return pressed >= 0 ? pressed == 1 : armed.load();
    }
    void pressRecord(bool on);
    void toggleSynthWindow();
    void startGuiTimer();
    void stopGuiTimer();
    void guiTick();

    void log(const char* format, ...) __attribute__((format(printf, 2, 3)));

    bool init();
    void destroy();
    void ensureMidi();
    void openInput();
    void chooseInput(const InputChoice& choice);
    std::vector<InputItem> inputItems();

    // P4: the synth.
    SynthChoice setupSynth() const;
    SynthChoice currentSynth() const;   // with its state as it is now
    std::unique_ptr<uw::HostedPlugin> loadSynth(const SynthChoice& choice, std::string& path);
    bool useSynth(const SynthChoice& choice);   // false if it can't change now
    void chooseSynth(const std::string& id);    // from the window: an undo step
    void collectRetired();
    const std::vector<uw::PluginDescriptor>& installedSynths();
    void housekeeping();
    void report();
    void logTake(const ClipRef& take);

    static Plugin* self(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }
};

// "Change synth" in the plugin's undo: the synth it holds is put back, with
// its state, and the step that puts back the one it replaced is returned.
class SynthStep final : public LaneKeeper::OtherStep {
public:
    SynthStep(Plugin& p, SynthChoice c) : plugin(p), held(std::move(c)) {}
    std::shared_ptr<LaneKeeper::OtherStep> swap() override {
        SynthChoice now = plugin.currentSynth();
        if (!plugin.useSynth(held)) return nullptr;
        return std::make_shared<SynthStep>(plugin, std::move(now));
    }

private:
    Plugin&     plugin;
    SynthChoice held;
};

void Plugin::log(const char* format, ...) {
    char    line[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    writeLog(instance, line);
}

Plugin::Plugin(const clap_host_t* h) : host(h), instance(nextInstance.fetch_add(1)) {}

// --- ForwardingHost and EditorBridge ----------------------------------------------------

void ForwardingHost::requestRestart() { plugin.host->request_restart(plugin.host); }
void ForwardingHost::requestProcess() { plugin.host->request_process(plugin.host); }

void ForwardingHost::log(uw::LogLevel level, const char* message) {
    if (level < uw::LogLevel::Warning) return;
    plugin.log("synth: %s", message);
    if (plugin.hostLog != nullptr) plugin.hostLog->log(plugin.host, CLAP_LOG_WARNING, message);
}

uw::HostInfo ForwardingHost::hostInfo() const {
    return {std::string("Umpire in ") + (plugin.host->name ? plugin.host->name : "a host"), "Unlikelyware",
            "https://unlikelyware.com", UMPIRE_VERSION_STRING};
}

void ForwardingHost::stateChanged() {
    if (plugin.hostState != nullptr) plugin.hostState->mark_dirty(plugin.host);
}

// While we're activating, the synth's new latency is read when its activate
// returns (pluginActivate). At any other time it shouldn't change without a
// restart (CLAP latency), so ask the host for one.
void ForwardingHost::latencyChanged() {
    if (!plugin.activating) plugin.host->request_restart(plugin.host);
}

uw::TimerId ForwardingHost::addTimer(std::uint32_t periodMs, std::function<void()> callback) {
    if (plugin.hostTimer == nullptr) return uw::kInvalidTimer;
    clap_id hostId = CLAP_INVALID_ID;
    if (!plugin.hostTimer->register_timer(plugin.host, periodMs, &hostId)) return uw::kInvalidTimer;
    const uw::TimerId id = plugin.nextTimer++;
    plugin.timers[hostId] = Plugin::Timer{id, std::move(callback)};
    plugin.log("synth registered a %u ms timer", periodMs);
    return id;
}

void ForwardingHost::removeTimer(uw::TimerId id) {
    for (auto it = plugin.timers.begin(); it != plugin.timers.end(); ++it) {
        if (it->second.id != id) continue;
        plugin.hostTimer->unregister_timer(plugin.host, it->first);
        plugin.timers.erase(it);
        return;
    }
}

bool ForwardingHost::watchFd(int fd, std::uint32_t events, std::function<void(std::uint32_t)> callback) {
    if (plugin.hostFd == nullptr || plugin.fds.count(fd) != 0) return false;
    if (!plugin.hostFd->register_fd(plugin.host, fd, events)) return false;
    plugin.fds[fd] = std::move(callback);
    plugin.log("synth watches fd %d", fd);
    return true;
}

bool ForwardingHost::modifyFd(int fd, std::uint32_t events) {
    return plugin.hostFd != nullptr && plugin.fds.count(fd) != 0 && plugin.hostFd->modify_fd(plugin.host, fd, events);
}

void ForwardingHost::unwatchFd(int fd) {
    if (plugin.hostFd == nullptr || plugin.fds.erase(fd) == 0) return;
    plugin.hostFd->unregister_fd(plugin.host, fd);
}

// --- life -------------------------------------------------------------------------------

bool Plugin::init() {
    hostLog   = static_cast<const clap_host_log_t*>(host->get_extension(host, CLAP_EXT_LOG));
    hostTimer = static_cast<const clap_host_timer_support_t*>(host->get_extension(host, CLAP_EXT_TIMER_SUPPORT));
    hostFd    = static_cast<const clap_host_posix_fd_support_t*>(host->get_extension(host, CLAP_EXT_POSIX_FD_SUPPORT));
    hostGui   = static_cast<const clap_host_gui_t*>(host->get_extension(host, CLAP_EXT_GUI));
    hostState = static_cast<const clap_host_state_t*>(host->get_extension(host, CLAP_EXT_STATE));
    hostLatency = static_cast<const clap_host_latency_t*>(host->get_extension(host, CLAP_EXT_LATENCY));
    hostParams  = static_cast<const clap_host_params_t*>(host->get_extension(host, CLAP_EXT_PARAMS));

    const fs::path confPath = configPath();
    config = parseConfig(readFile(confPath));
    log("Umpire %s in %s %s; setup from %s: input '%s', synth '%s'", UMPIRE_VERSION_STRING,
        host->name ? host->name : "?", host->version ? host->version : "?", confPath.c_str(),
        config.input.empty() ? "(choose)" : config.input.c_str(), config.synth.c_str());
    log("host gives timer-support: %s, posix-fd-support: %s, gui: %s", hostTimer ? "yes" : "no",
        hostFd ? "yes" : "no", hostGui ? "yes" : "no");

    useSynth(setupSynth());

    if (config.input != "none") {
        ensureMidi();
        if (midi) openInput();
    }

    const RecordMode mode = recordMode(config.record, host->name ? host->name : "");
    engine.setRecordMode(mode);
    const char* why = !config.record.empty() ? "the setup file" : "by the host";
    if (mode == RecordMode::Play)
        log("record on play (%s): a take starts when the host plays with Record on", why);
    else if (mode == RecordMode::Off)
        log("record off (%s): a player, nothing is recorded", why);

    if (hostTimer != nullptr && !hostTimer->register_timer(host, 50, &ownTimer)) ownTimer = CLAP_INVALID_ID;
    keeper.start();   // takes reach the lane with or without the host's main thread
    return true;
}

void Plugin::destroy() {
    keeper.stop();
    if (midi) {   // no more input before the engine goes
        if (source != uw::kInvalidMidiSource) midi->close(source);
        midi.reset();
    }
    stopGuiTimer();
    window.reset();
    synthWindow.reset();   // closes the synth's editor
    engine.deactivate();   // the host should have already; harmless if so
    collectRetired();
    retiring.clear();
    synth.reset();         // unregisters the synth's timers and fds, through ForwardingHost
    factory.reset();       // after the synth, which holds its module
    if (hostTimer != nullptr) {
        for (const auto& [hostId, timer] : timers) hostTimer->unregister_timer(host, hostId);
        if (ownTimer != CLAP_INVALID_ID) hostTimer->unregister_timer(host, ownTimer);
    }
    if (hostFd != nullptr)
        for (const auto& [fd, callback] : fds) hostFd->unregister_fd(host, fd);
    log("closed");
}

// The MIDI system, made when an input is first wanted (at init, or when the
// window lists the sources).
void Plugin::ensureMidi() {
    if (midi) return;
    midi = uw::PlatformMidiInput::create("Umpire");
    if (!midi) {
        log("couldn't reach %s", "the MIDI system");
        return;
    }
    midi->setSink(&engine);
    seenChanges = midi->changes();
}

void Plugin::openInput() {
    nextInputTry = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    if (!midi || inputChoice.mode == InputMode::None) return;
    const std::vector<uw::MidiSourceInfo> sources = midi->enumerate();
    int chosen = -1;
    if (inputChoice.mode == InputMode::Setup) {
        chosen = chooseSource(sources, config.input);
    } else {   // the source chosen: by its id, else by its name; otherwise it's missing, and waited for
        for (std::size_t i = 0; i < sources.size() && chosen < 0; ++i)
            if (sources[i].id == inputChoice.id) chosen = static_cast<int>(i);
        for (std::size_t i = 0; i < sources.size() && chosen < 0; ++i)
            if (sources[i].name == inputChoice.name) chosen = static_cast<int>(i);
        inputMissing = chosen < 0;
    }
    const std::string wanted = inputChoice.mode == InputMode::Setup ? config.input : inputChoice.name;
    if (chosen < 0) {
        if (sourceName != "-") {   // said once, not every retry
            std::string names;
            for (const auto& s : sources) names += (names.empty() ? "" : ", ") + s.name;
            log("no input to open (wanted '%s'); there are: %s", wanted.c_str(), names.empty() ? "none" : names.c_str());
            sourceName = "-";
        }
        return;
    }
    const uw::MidiSourceInfo& info = sources[static_cast<std::size_t>(chosen)];
    source = midi->open(info.id);
    if (source == uw::kInvalidMidiSource) {
        log("couldn't open input '%s'", info.name.c_str());
        return;
    }
    sourceName      = info.name;
    sourceConnected = true;
    inputMissing    = false;
    log("input: '%s' (%s), %s", info.name.c_str(), info.id.c_str(), midi->describe(source).c_str());
}

// Another input (the window's menu, or a loaded state): the one open closes,
// and the new one opens now or, if it isn't here, when it comes.
void Plugin::chooseInput(const InputChoice& choice) {
    if (midi && source != uw::kInvalidMidiSource) midi->close(source);
    source          = uw::kInvalidMidiSource;
    sourceName.clear();
    sourceConnected = false;
    inputMissing    = false;
    inputChoice     = choice;
    log("input chosen: %s", choice.mode == InputMode::Setup ? "the default"
                            : choice.mode == InputMode::None ? "none"
                                                             : ("'" + choice.name + "'").c_str());
    if (choice.mode == InputMode::None) return;
    ensureMidi();
    openInput();
}

// The window's input menu: the default (what it opens), each source, a
// chosen one that isn't here, and none.
std::vector<InputItem> Plugin::inputItems() {
    ensureMidi();
    const std::vector<uw::MidiSourceInfo> sources = midi ? midi->enumerate() : std::vector<uw::MidiSourceInfo>{};
    std::vector<InputItem>                items;
    const int   fallback = chooseSource(sources, config.input);
    std::string label    = "Default (" + (fallback >= 0 ? sources[static_cast<std::size_t>(fallback)].name
                                                         : std::string(config.input == "none" ? "none" : "none found")) + ")";
    items.push_back({label, InputChoice{}, inputChoice.mode == InputMode::Setup});
    bool listed = false;
    for (const uw::MidiSourceInfo& s : sources) {
        const bool on = inputChoice.mode == InputMode::Named && (s.id == inputChoice.id || (!inputMissing && sourceName == s.name));
        listed        = listed || on;
        items.push_back({s.name, InputChoice{InputMode::Named, s.name, s.id}, on});
    }
    if (inputChoice.mode == InputMode::Named && !listed) items.push_back({inputChoice.name + " (not here)", inputChoice, true});
    items.push_back({"None", InputChoice{InputMode::None, {}, {}}, inputChoice.mode == InputMode::None});
    return items;
}

// --- the synth (P4) ---------------------------------------------------------------------

SynthChoice Plugin::setupSynth() const {
    SynthChoice c;
    if (config.synth == "none") return c;
    c.id   = config.synthId;
    c.name = config.synth;   // a .clap's name in CLAP's folders, or a path
    return c;
}

SynthChoice Plugin::currentSynth() const {
    if (!synth) {   // none, or one that isn't here, as it was kept
        SynthChoice c = missingSynth;
        c.chosen      = synthChosen;
        return c;
    }
    SynthChoice c;
    c.chosen = synthChosen;
    c.id     = synth->descriptor().id;
    c.path   = synthPath;
    c.name   = synth->descriptor().name;
    if (!synth->saveState(c.state)) c.state.clear();
    return c;
}

// Every CLAP instrument in CLAP's folders, but Umpire: scanned once for the
// process, when the window's synth menu first opens. Scanning loads each
// .clap into the host's process, so nothing else asks for it: loading a
// state never does.
struct SynthScan {
    std::vector<uw::PluginDescriptor> found;
    bool                              done = false;
};
SynthScan& synthScan() {
    static SynthScan scan;
    return scan;
}

const std::vector<uw::PluginDescriptor>& Plugin::installedSynths() {
    SynthScan& scan = synthScan();
    if (scan.done) return scan.found;
    if (!factory) factory = sharedFactory();
    for (uw::PluginDescriptor& d : factory->scan())
        if (d.kind == uw::PluginKind::Instrument && d.id != kOwnId) scan.found.push_back(std::move(d));
    std::sort(scan.found.begin(), scan.found.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    scan.done = true;
    log("found %zu instruments in CLAP's folders", scan.found.size());
    return scan.found;
}

// The synth a choice names: its .clap where it was, else a .clap of that
// file's name or the synth's name in CLAP's folders, else the setup file's
// (an older state names its synth by id alone); then the plugin in it with
// its id (or its first). Failing that, an instrument with its id among those
// the synth menu found, if it's been opened. Null: none named, or not here.
std::unique_ptr<uw::HostedPlugin> Plugin::loadSynth(const SynthChoice& c, std::string& path) {
    path.clear();
    if (c.id.empty() && c.path.empty() && c.name.empty()) return nullptr;
    if (!factory) factory = sharedFactory();
    std::error_code          ec;
    std::vector<std::string> files;
    const auto               add = [&](const std::string& name) {
        if (name.empty()) return;
        if (std::string f = findClap(factory->searchPaths(), name); !f.empty() &&
                                                                    std::find(files.begin(), files.end(), f) == files.end())
            files.push_back(f);
    };
    if (!c.path.empty() && fs::exists(c.path, ec)) files.push_back(c.path);
    if (!c.path.empty()) add(fs::path(c.path).stem().string());
    add(c.name);
    if (!c.id.empty() && config.synth != "none") add(config.synth);
    std::optional<uw::PluginDescriptor> desc;
    for (const std::string& file : files) {
        for (const uw::PluginDescriptor& d : factory->scanFile(file))
            if ((c.id.empty() || d.id == c.id) && d.id != kOwnId) {
                desc = d;
                break;
            }
        if (desc) break;
    }
    if (!desc && !c.id.empty())
        for (const uw::PluginDescriptor& d : synthScan().found)
            if (d.id == c.id) {
                desc = d;
                break;
            }
    if (!desc) return nullptr;
    std::unique_ptr<uw::HostedPlugin> made = factory->create(*desc, services);
    if (!made) log("synth %s (%s) wouldn't load, from %s", desc->name.c_str(), desc->id.c_str(), desc->path.c_str());
    else path = desc->path;
    return made;
}

// Hosts the synth `c` names, with its state, in place of the one now: at once
// if the plugin isn't active, else from the audio thread's next block (the
// old one is let go of there, and destroyed in collectRetired()). One that
// isn't here is kept as it was, state and all, and the plugin is silent.
bool Plugin::useSynth(const SynthChoice& c) {
    std::string                       path;
    std::unique_ptr<uw::HostedPlugin> next = loadSynth(c, path);
    if (next && !c.state.empty() && !next->loadState(c.state.data(), c.state.size()))
        log("%s didn't take its saved state", next->descriptor().name.c_str());
    const PluginEngine::Swap swap = engine.swapSynth(next.get());
    if (swap == PluginEngine::Swap::Busy) {   // nothing changed: the old synth's editor stays
        log("the synth can't change now: the host hasn't processed the last change yet");
        return false;
    }
    if (synthWindow) {   // the old synth's editor goes with it, before it might be destroyed below
        synthWindow.reset();
        log("synth's window closed");
    }
    if (swap == PluginEngine::Swap::Pending && synth) retiring.push_back(std::move(synth));
    synth       = std::move(next);   // Done: the old one goes now
    synthPath   = path;
    synthChosen = c.chosen;
    missingSynth = {};
    if (synth) {
        log("synth: %s (%s), from %s", synth->descriptor().name.c_str(), synth->descriptor().id.c_str(), path.c_str());
    } else if (!c.id.empty() || !c.name.empty()) {
        missingSynth = c;
        log("synth %s (%s) isn't here: none, and its state is kept", c.name.c_str(), c.id.c_str());
    } else {
        log("synth: none");
    }
    if (engine.active()) {
        // The latency is read at activation: a new one needs the host to restart us.
        const std::uint32_t now = synth && engine.synthHasNotes() ? synth->latencyFrames() : 0;
        if (now != latency) host->request_restart(host);
        host->request_process(host);   // so the audio thread takes it, even if the host let it sleep
    }
    return true;
}

void Plugin::chooseSynth(const std::string& id) {
    const bool same = synth ? synth->descriptor().id == id : (id.empty() && missingSynth.id.empty() && missingSynth.name.empty());
    if (same) return;
    SynthChoice was = currentSynth();
    SynthChoice next;
    next.chosen = true;
    next.id     = id;
    for (const uw::PluginDescriptor& d : installedSynths())
        if (d.id == id) {
            next.path = d.path;
            next.name = d.name;
        }
    if (!useSynth(next)) return;
    keeper.recordOther("Change synth", std::make_shared<SynthStep>(*this, std::move(was)));
}

// Synths the audio thread has let go of: destroyed here, on the main thread.
void Plugin::collectRetired() {
    while (uw::HostedPlugin* done = engine.retiredSynth())
        std::erase_if(retiring, [done](const auto& p) { return p.get() == done; });
}

// The main thread's work, from our timer and from main-thread callbacks.
void Plugin::housekeeping() {
    collectRetired();
    if (synth) synth->idle();
    if (synthWindow && !synthWindow->isOpen()) {   // closed by its close button
        synthWindow.reset();
        log("synth's window closed");
    }

    // Takes reach the lane on the lane thread; the host is told here, since
    // mark_dirty is the main thread's.
    if (keeper.takeDirty() && hostState != nullptr) hostState->mark_dirty(host);
    if (const std::uint64_t missed = engine.takeMissedCount(); missed > 0)
        log("%llu take(s) not recorded: the last two weren't collected yet", static_cast<unsigned long long>(missed));

    if (armed.load() != loggedArmed) {
        loggedArmed = armed.load();
        log(loggedArmed ? "record: armed" : "record: off");
    }
    // Which of the host's transport flags it gives, when they change: recording
    // needs playing, beats and recording (with the arm), so a host that leaves
    // one out shows here.
    if (const std::uint32_t f = transportFlags.load(std::memory_order_relaxed); f != loggedTransportFlags) {
        loggedTransportFlags = f;
        if (f == ~0u) {
            log("transport: none from the host");
        } else {
            log("transport: %s%s%s%s%s%s%s%s",
                (f & CLAP_TRANSPORT_IS_PLAYING) ? "playing" : "stopped",
                (f & CLAP_TRANSPORT_IS_RECORDING) ? ", recording" : "",
                (f & CLAP_TRANSPORT_IS_WITHIN_PRE_ROLL) ? ", pre-roll" : "",
                (f & CLAP_TRANSPORT_IS_LOOP_ACTIVE) ? ", loop" : "",
                (f & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) ? ", beats" : ", no beats",
                (f & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE) ? ", seconds" : "",
                (f & CLAP_TRANSPORT_HAS_TEMPO) ? ", tempo" : "",
                (f & CLAP_TRANSPORT_HAS_TIME_SIGNATURE) ? ", meter" : "");
        }
    }
    if (engine.recording() != wasRecording) {
        wasRecording = engine.recording();
        log(wasRecording ? "recording" : "recording stopped");
    }

    if (midi) {
        const auto now = std::chrono::steady_clock::now();
        if (source == uw::kInvalidMidiSource && now >= nextInputTry) openInput();
        if (source != uw::kInvalidMidiSource && midi->changes() != seenChanges) {
            seenChanges          = midi->changes();
            const bool connected = midi->sync(source);
            if (connected != sourceConnected) log("input '%s' %s", sourceName.c_str(), connected ? "is back" : "went away");
            sourceConnected = connected;
        }
    }
    if (std::chrono::steady_clock::now() - lastReport >= std::chrono::seconds(10)) report();
}

// On the lane thread (or wherever the take was collected), with the lane's
// lock held: writeLog() takes its own.
void Plugin::logTake(const ClipRef& take) {
    const Clip& clip  = *take.clip;
    const auto  notes = notesOf(clip.events, clip.length);
    const auto  beats = [](Tick t) { return static_cast<double>(t) / static_cast<double>(kTicksPerQuarter); };
    Tick        firstOn = -1;
    for (const ClipEvent& e : clip.events)
        if (uw::ump::isNoteOn(e.message)) {
            firstOn = take.start + e.tick;
            break;
        }
    log("%s: %zu notes, %zu events, from beat %.2f to beat %.2f, first note-on at beat %.2f, at %.1f bpm",
        clip.name.c_str(), notes.size(), clip.events.size(), beats(take.start), beats(take.end()),
        firstOn < 0 ? -1.0 : beats(firstOn), engine.lastTempo());
}

// A heartbeat every 10 s: what the main thread has been given, and the live
// input's timing.
void Plugin::report() {
    lastReport = std::chrono::steady_clock::now();
    if (!activeOnce) return;
    log("heartbeat: %llu timer ticks, %llu main-thread callbacks, %llu synth timer ticks; window %s, synth's window %s; %s",
        static_cast<unsigned long long>(timerTicks), static_cast<unsigned long long>(callbacks),
        static_cast<unsigned long long>(synthTimerTicks), window ? "open" : "closed", synthWindow ? "open" : "closed",
        engine.offline() ? "offline render" : "real time");
    timerTicks = callbacks = synthTimerTicks = 0;
    const LiveStats s = engine.takeStats();
    if (s.count > 0 || s.dropped > 0)
        log("live input: %llu messages, capture to block %.2f ms mean (%.2f to %.2f), %llu over a block late, %llu dropped",
            static_cast<unsigned long long>(s.count), s.meanMs, s.minMs, s.maxMs,
            static_cast<unsigned long long>(s.clamped), static_cast<unsigned long long>(s.dropped));
}

// --- clap_plugin ------------------------------------------------------------------------

bool pluginInit(const clap_plugin_t* p) { return Plugin::self(p)->init(); }

void pluginDestroy(const clap_plugin_t* p) {
    Plugin* self = Plugin::self(p);
    self->destroy();
    delete self;
}

bool pluginActivate(const clap_plugin_t* p, double sampleRate, std::uint32_t, std::uint32_t maxFrames) {
    Plugin* self     = Plugin::self(p);
    self->activating = true;
    if (!self->engine.activate(sampleRate, maxFrames)) self->log("the synth wouldn't activate: silence");
    self->activating = false;
    // The synth's latency is ours; the host may be told of a change only now.
    const std::uint32_t latency = self->synth && self->engine.synthHasNotes() ? self->synth->latencyFrames() : 0;
    if (latency != self->latency) {
        self->latency = latency;
        if (self->hostLatency != nullptr) self->hostLatency->changed(self->host);
        self->log("latency: %u frames (the synth's)", latency);
    }
    self->callbackFrames      = sampleRate * 0.05;
    self->framesSinceCallback = 0;
    self->activeOnce          = true;
    self->log("active at %.0f Hz, blocks up to %u; the synth takes %s", sampleRate, maxFrames,
              !self->engine.synthHasNotes() ? "no notes" : self->engine.synthTakesMidi2() ? "MIDI 2.0" : "only MIDI 1.0");
    return true;
}

void pluginDeactivate(const clap_plugin_t* p) {
    Plugin* self = Plugin::self(p);
    self->engine.deactivate();
    self->collectRetired();
}
bool pluginStartProcessing(const clap_plugin_t*) { return true; }
void pluginStopProcessing(const clap_plugin_t* p) { Plugin::self(p)->engine.stopProcessing(); }
void pluginReset(const clap_plugin_t* p) { Plugin::self(p)->engine.reset(); }

HostTransport fromClap(const clap_event_transport_t& t) {
    HostTransport h;
    h.playing       = (t.flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
    h.recording     = (t.flags & CLAP_TRANSPORT_IS_RECORDING) != 0 && (t.flags & CLAP_TRANSPORT_IS_WITHIN_PRE_ROLL) == 0;
    h.hasBeats      = (t.flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0;
    h.songBeats     = static_cast<double>(t.song_pos_beats) / CLAP_BEATTIME_FACTOR;
    h.barStartBeats = static_cast<double>(t.bar_start) / CLAP_BEATTIME_FACTOR;
    h.barNumber     = t.bar_number;
    if ((t.flags & CLAP_TRANSPORT_HAS_TEMPO) != 0) h.tempo = t.tempo;
    if ((t.flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE) != 0) {
        h.tsigNum = t.tsig_num;
        h.tsigDen = t.tsig_denom;
    }
    return h;
}

// The Record parameter's id (stable: hosts save automation by it).
constexpr clap_id kRecordParam = 1;

// A param event for the Record parameter: its new value in `armed`.
// The Record button's press, to the host as a gesture, so it's recorded as
// automation and the host's view of the parameter follows.
void pushArm(const clap_output_events_t* out, bool armed) {
    if (out == nullptr) return;
    clap_event_param_gesture_t gesture{};
    gesture.header.size     = sizeof gesture;
    gesture.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    gesture.header.type     = CLAP_EVENT_PARAM_GESTURE_BEGIN;
    gesture.param_id        = kRecordParam;
    out->try_push(out, &gesture.header);
    clap_event_param_value_t value{};
    value.header.size     = sizeof value;
    value.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    value.header.type     = CLAP_EVENT_PARAM_VALUE;
    value.param_id        = kRecordParam;
    value.note_id         = -1;
    value.port_index      = -1;
    value.channel         = -1;
    value.key             = -1;
    value.value           = armed ? 1.0 : 0.0;
    out->try_push(out, &value.header);
    gesture.header.type = CLAP_EVENT_PARAM_GESTURE_END;
    out->try_push(out, &gesture.header);
}

bool readArm(const clap_event_header_t* h, bool& armed) {
    if (h == nullptr || h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) return false;
    const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
    if (e->param_id != kRecordParam) return false;
    armed = e->value >= 0.5;
    return true;
}

clap_process_status pluginProcess(const clap_plugin_t* p, const clap_process_t* process) {
    Plugin* self = Plugin::self(p);
    Block   b;
    b.frames = process->frames_count;
    b.now    = uw::nowNanoseconds();
    if (process->transport != nullptr) b.transport = fromClap(*process->transport);
    self->transportFlags.store(process->transport != nullptr ? process->transport->flags : ~0u,
                               std::memory_order_relaxed);
    bool armed = self->armed.load(std::memory_order_relaxed);
    if (const int pressed = self->guiArm.exchange(-1); pressed >= 0) {   // the window's Record button
        armed = pressed == 1;
        pushArm(process->out_events, armed);
    }
    b.armed    = armed;
    // Transport events inside the block (a tempo change) and the arm's
    // changes split it (the engine). Past the array, a change of the arm
    // still applies from the next block.
    if (const clap_input_events_t* in = process->in_events) {
        const std::uint32_t n = in->size(in);
        for (std::uint32_t i = 0; i < n; ++i) {
            const clap_event_header_t* h = in->get(in, i);
            TransportChange            change;
            if (readArm(h, armed)) {
                change.hasTransport = false;
            } else if (h != nullptr && h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_TRANSPORT) {
                change.transport = fromClap(*reinterpret_cast<const clap_event_transport_t*>(h));
            } else {
                continue;
            }
            change.frame = h->time;
            change.armed = armed;
            if (b.changeCount < self->changes.size()) self->changes[b.changeCount++] = change;
        }
        b.changes = self->changes.data();
    }
    self->armed.store(armed, std::memory_order_relaxed);
    if (process->audio_outputs_count > 0 && process->audio_outputs[0].data32 != nullptr) {
        b.out         = process->audio_outputs[0].data32;
        b.outChannels = process->audio_outputs[0].channel_count;
        process->audio_outputs[0].constant_mask = 0;
    }
    self->engine.process(b);

    // The main thread is asked in now and then, so the synth's callbacks are
    // seen to on hosts without timer-support.
    self->framesSinceCallback += b.frames;
    if (self->framesSinceCallback >= self->callbackFrames) {
        self->framesSinceCallback = 0;
        self->host->request_callback(self->host);
    }
    return CLAP_PROCESS_CONTINUE;
}

void pluginOnMainThread(const clap_plugin_t* p) {
    Plugin* self = Plugin::self(p);
    ++self->callbacks;
    self->housekeeping();
}

// --- audio ports and note ports: stereo out, no notes from the host ---------------------

std::uint32_t audioPortsCount(const clap_plugin_t*, bool isInput) { return isInput ? 0 : 1; }

bool audioPortsGet(const clap_plugin_t*, std::uint32_t index, bool isInput, clap_audio_port_info_t* info) {
    if (isInput || index != 0) return false;
    info->id            = 0;
    std::snprintf(info->name, sizeof info->name, "%s", "Main");
    info->flags         = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type     = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t kAudioPorts = {audioPortsCount, audioPortsGet};

std::uint32_t notePortsCount(const clap_plugin_t*, bool) { return 0; }
bool notePortsGet(const clap_plugin_t*, std::uint32_t, bool, clap_note_port_info_t*) { return false; }
const clap_plugin_note_ports_t kNotePorts = {notePortsCount, notePortsGet};

// --- state: the lane's takes as MIDI Clip Files, and the synth's own --------------------

bool stateSave(const clap_plugin_t* p, const clap_ostream_t* stream) {
    Plugin* self = Plugin::self(p);
    self->keeper.pump();   // a take just finished goes in
    SavedState saved;
    std::size_t clipBytes = 0;
    const clipfile::Tempo tempo{self->engine.lastTempo(), self->engine.lastBeatsPerBar()};
    for (const ClipRef& take : self->keeper.takes()) {
        SavedTake t;
        t.start    = take.start;
        t.clipFile = clipfile::write(take.clip->events, take.clip->length, tempo);
        t.name     = take.clip->name;
        t.scale    = take.clip->scale;
        t.scaleFit = take.clip->scaleFit;
        clipBytes += t.clipFile.size();
        saved.takes.push_back(std::move(t));
    }
    saved.nextTakeNumber = static_cast<std::uint32_t>(self->keeper.nextTakeNumber());
    // The synth: the one hosted, or one chosen that isn't here, kept as it was.
    SynthChoice synth  = self->currentSynth();
    saved.synthChosen  = synth.chosen;
    saved.synthId      = std::move(synth.id);
    saved.synthPath    = std::move(synth.path);
    saved.synthName    = std::move(synth.name);
    saved.synthState   = std::move(synth.state);
    saved.armed        = self->armed.load();
    saved.input        = self->inputChoice;
    saved.zoom         = self->zoom;
    const std::vector<std::uint8_t> bytes = encodeState(saved);
    std::size_t                     at    = 0;
    while (at < bytes.size()) {
        const std::int64_t n = stream->write(stream, bytes.data() + at, bytes.size() - at);
        if (n <= 0) return false;
        at += static_cast<std::size_t>(n);
    }
    self->log("saved: %zu takes, %zu bytes of clips, synth %s, state %zu bytes, record %s, zoom %.2f lane, %.2f grid",
              saved.takes.size(), clipBytes, saved.synthId.empty() ? "none" : saved.synthId.c_str(), saved.synthState.size(),
              saved.armed ? "on" : "off", static_cast<double>(saved.zoom.lane), static_cast<double>(saved.zoom.grid));
    return true;
}

bool stateLoad(const clap_plugin_t* p, const clap_istream_t* stream) {
    Plugin*                   self = Plugin::self(p);
    std::vector<std::uint8_t> bytes;
    std::uint8_t              chunk[4096];
    for (;;) {
        const std::int64_t n = stream->read(stream, chunk, sizeof chunk);
        if (n < 0) return false;
        if (n == 0) break;
        bytes.insert(bytes.end(), chunk, chunk + n);
    }
    SavedState  saved;
    std::string error;
    if (!decodeState(bytes.data(), bytes.size(), saved, error)) {
        self->log("couldn't load the state: %s", error.c_str());
        return false;
    }
    std::vector<ClipRef> takes;
    std::size_t          events = 0;
    for (std::size_t i = 0; i < saved.takes.size(); ++i) {
        const clipfile::Read read = clipfile::read(saved.takes[i].clipFile.data(), saved.takes[i].clipFile.size());
        if (!read.ok) {
            self->log("couldn't read the saved take %zu: %s", i + 1, read.error.c_str());
            return false;
        }
        auto clip    = std::make_shared<Clip>();
        clip->events = read.events;
        clip->length = read.length;
        clip->name   = saved.takes[i].name;   // empty (an older state): "Take n"
        workOutScale(*clip);                  // the candidates, and the scale unless it was set by hand
        if (saved.takes[i].scaleFit == ScaleFit::Chosen) {
            clip->scale    = saved.takes[i].scale;
            clip->scaleFit = ScaleFit::Chosen;
        }
        events += clip->events.size();
        ClipRef ref;
        ref.start = saved.takes[i].start;
        ref.clip  = std::move(clip);
        takes.push_back(std::move(ref));
    }
    const std::size_t takeCount = takes.size();
    self->keeper.setTakes(std::move(takes), static_cast<int>(saved.nextTakeNumber));
    self->armed.store(saved.armed);
    if (const auto* params = static_cast<const clap_host_params_t*>(self->host->get_extension(self->host, CLAP_EXT_PARAMS)))
        params->rescan(self->host, CLAP_PARAM_RESCAN_VALUES);

    // The synth: the one the state chose, else the setup file's. The one
    // hosted now stays if it's that one, and takes the state's.
    const bool same = saved.synthChosen ? (self->synth ? self->synth->descriptor().id == saved.synthId
                                                       : saved.synthId.empty() && self->missingSynth.id.empty() &&
                                                             self->missingSynth.name.empty())
                                        : !self->synthChosen;
    bool synthLoaded = false;
    if (same) {
        self->synthChosen = saved.synthChosen;
        if (self->synth && !saved.synthState.empty()) {
            if (saved.synthId != self->synth->descriptor().id)
                self->log("the state's synth is %s, not %s: its state isn't loaded", saved.synthId.c_str(),
                          self->synth->descriptor().id.c_str());
            else synthLoaded = self->synth->loadState(saved.synthState.data(), saved.synthState.size());
        }
    } else if (saved.synthChosen) {
        SynthChoice wanted{true, saved.synthId, saved.synthPath, saved.synthName, saved.synthState};
        self->useSynth(wanted);
        synthLoaded = self->synth && !saved.synthState.empty();
    } else {   // back to the setup file's: the state's synth state, if it's that one's
        self->useSynth(self->setupSynth());
        if (self->synth && !saved.synthState.empty() && saved.synthId == self->synth->descriptor().id)
            synthLoaded = self->synth->loadState(saved.synthState.data(), saved.synthState.size());
    }
    if (!(saved.input == self->inputChoice)) self->chooseInput(saved.input);
    self->zoom = saved.zoom;   // an open window reads it as it draws
    self->log("loaded: %zu takes, %zu events; synth %s, state %s; record %s", takeCount, events,
              self->synth ? self->synth->descriptor().name.c_str()
                          : self->missingSynth.name.empty() ? "none" : (self->missingSynth.name + " (not here)").c_str(),
              synthLoaded ? "loaded" : "not loaded", saved.armed ? "on" : "off");
    return true;
}

const clap_plugin_state_t kState = {stateSave, stateLoad};

// --- params: Record, the arm ------------------------------------------------------------

// One parameter, so a host can arm an instance (its parameter view, an
// automation lane) and the others don't record (plugin-version.md, "The arm").

std::uint32_t paramsCount(const clap_plugin_t*) { return 1; }

bool paramsGetInfo(const clap_plugin_t*, std::uint32_t index, clap_param_info_t* info) {
    if (index != 0) return false;
    *info               = {};
    info->id            = kRecordParam;
    info->flags         = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;
    info->cookie        = nullptr;
    std::snprintf(info->name, sizeof info->name, "%s", "Record");
    info->module[0]     = '\0';
    info->min_value     = 0;
    info->max_value     = 1;
    info->default_value = 1;
    return true;
}

bool paramsGetValue(const clap_plugin_t* p, clap_id id, double* value) {
    if (id != kRecordParam) return false;
    *value = Plugin::self(p)->armed.load() ? 1.0 : 0.0;
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* text, std::uint32_t size) {
    if (id != kRecordParam || size == 0) return false;
    std::snprintf(text, size, "%s", value >= 0.5 ? "On" : "Off");
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* text, double* value) {
    if (id != kRecordParam) return false;
    if (std::strcmp(text, "On") == 0 || std::strcmp(text, "on") == 0 || std::strcmp(text, "1") == 0) *value = 1;
    else if (std::strcmp(text, "Off") == 0 || std::strcmp(text, "off") == 0 || std::strcmp(text, "0") == 0) *value = 0;
    else return false;
    return true;
}

// Param events outside process() (the host isn't processing): the arm
// applies from the next block.
void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t* out) {
    Plugin* self  = Plugin::self(p);
    bool    armed = self->armed.load();
    if (const int pressed = self->guiArm.exchange(-1); pressed >= 0) {
        armed = pressed == 1;
        pushArm(out, armed);
    }
    if (in != nullptr)
        for (std::uint32_t i = 0, n = in->size(in); i < n; ++i) readArm(in->get(in, i), armed);
    self->armed.store(armed);
}

const clap_plugin_params_t kParams = {paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText,
                                      paramsTextToValue, paramsFlush};

// --- latency: the synth's, as ours ------------------------------------------------------

std::uint32_t latencyGet(const clap_plugin_t* p) { return Plugin::self(p)->latency; }

const clap_plugin_latency_t kLatency = {latencyGet};

// --- render: offline, live input is ignored ---------------------------------------------

bool renderHardRealtime(const clap_plugin_t*) { return false; }

bool renderSet(const clap_plugin_t* p, clap_plugin_render_mode mode) {
    Plugin* self = Plugin::self(p);
    self->engine.setOffline(mode == CLAP_RENDER_OFFLINE);
    self->log(mode == CLAP_RENDER_OFFLINE ? "render: offline (live input ignored)" : "render: real time");
    return true;
}

const clap_plugin_render_t kRender = {renderHardRealtime, renderSet};

// --- timer-support and posix-fd-support: ours, and the synth's forwarded ----------------

void onTimer(const clap_plugin_t* p, clap_id id) {
    Plugin* self = Plugin::self(p);
    if (id == self->ownTimer) {
        ++self->timerTicks;
        self->housekeeping();
        return;
    }
    if (id == self->guiTimer) {
        self->guiTick();
        return;
    }
    const auto it = self->timers.find(id);
    if (it == self->timers.end()) return;
    ++self->synthTimerTicks;
    const std::function<void()> callback = it->second.callback;   // a copy: it may remove itself
    callback();
}

const clap_plugin_timer_support_t kTimerSupport = {onTimer};

void onFd(const clap_plugin_t* p, int fd, clap_posix_fd_flags_t flags) {
    Plugin*    self = Plugin::self(p);
    const auto it   = self->fds.find(fd);
    if (it == self->fds.end()) return;
    const std::function<void(std::uint32_t)> callback = it->second;
    callback(flags);
}

const clap_plugin_posix_fd_support_t kPosixFdSupport = {onFd};

// --- gui: Umpire's window (P3), embedded; the synth's in a window of its own -----------

// The Record button: applied on the audio thread at the next block (or by
// params flush, if the host isn't processing), which tells the host.
void Plugin::pressRecord(bool on) {
    guiArm.store(on ? 1 : 0);
    if (hostParams != nullptr) hostParams->request_flush(host);
}

void Plugin::toggleSynthWindow() {
    if (synthWindow) {
        synthWindow.reset();
        log("synth's window closed");
        return;
    }
    if (!synth || !synth->hasEditor()) return;
    synthWindow = uw::EditorWindow::open(*synth, services, "Umpire - " + synth->descriptor().name);
    log(synthWindow ? "synth's window opened" : "the synth's editor wouldn't open");
}

// The window's timer, about 60 a second: the host's if it has timer-support,
// else (Bitwig on the Mac) one of our own on the main run loop, in the
// common modes so it keeps firing during a drag, as Retune's.
void Plugin::startGuiTimer() {
    if (guiTimer != CLAP_INVALID_ID) return;
    if (hostTimer != nullptr && hostTimer->register_timer(host, 16, &guiTimer)) return;
    guiTimer = CLAP_INVALID_ID;
#ifdef __APPLE__
    if (ownGuiTimer != nullptr) return;
    CFRunLoopTimerContext context = {0, this, nullptr, nullptr, nullptr};
    ownGuiTimer = CFRunLoopTimerCreate(
        kCFAllocatorDefault, CFAbsoluteTimeGetCurrent() + 0.016, 0.016, 0, 0,
        [](CFRunLoopTimerRef, void* info) { static_cast<Plugin*>(info)->guiTick(); }, &context);
    if (ownGuiTimer != nullptr) CFRunLoopAddTimer(CFRunLoopGetMain(), ownGuiTimer, kCFRunLoopCommonModes);
#endif
}

void Plugin::stopGuiTimer() {
    if (guiTimer != CLAP_INVALID_ID && hostTimer != nullptr) hostTimer->unregister_timer(host, guiTimer);
    guiTimer = CLAP_INVALID_ID;
#ifdef __APPLE__
    if (ownGuiTimer != nullptr) {
        CFRunLoopTimerInvalidate(ownGuiTimer);
        CFRelease(ownGuiTimer);
        ownGuiTimer = nullptr;
    }
#endif
}

void Plugin::guiTick() {
    if (!window) return;
#ifdef __APPLE__
    // Moved to a screen of another scale, or sized with the wrong screen's
    // (opened before the view was in the host's window, when the main screen
    // was another scale, or mid-drag): sized again, in this screen's pixels.
    if (const double now = parentScale(guiParent); now != guiScale || !window->fits(guiWidth, guiHeight)) {
        guiScale = now;
        window->setSize(guiWidth, guiHeight, guiScale);
    }
#endif
    window->idle();
}

bool guiIsApiSupported(const clap_plugin_t*, const char* api, bool isFloating) {
    return !isFloating && std::strcmp(api, kWindowApi) == 0;
}

bool guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* isFloating) {
    *api        = kWindowApi;
    *isFloating = false;
    return true;
}

// The window itself waits for set_parent, which gives it somewhere to be.
bool guiCreate(const clap_plugin_t* p, const char* api, bool isFloating) {
    if (!guiIsApiSupported(p, api, isFloating)) return false;
    Plugin::self(p)->guiCreated = true;
    return true;
}

void guiDestroy(const clap_plugin_t* p) {
    Plugin* self = Plugin::self(p);
    self->stopGuiTimer();
    if (self->window) self->log("window closed");
    self->window.reset();
    self->guiCreated = false;
    self->guiParent  = 0;
}

// The Mac's sizes are points (the backing scale is ours to apply); X11's are
// pixels, and set_scale says how many to a logical one.
bool guiSetScale(const clap_plugin_t* p, double scale) {
#ifdef __APPLE__
    (void)p, (void)scale;
    return false;
#else
    Plugin* self   = Plugin::self(p);
    self->guiScale = scale > 0 ? scale : 1.0;
    return true;
#endif
}

bool guiGetSize(const clap_plugin_t* p, std::uint32_t* width, std::uint32_t* height) {
    Plugin* self = Plugin::self(p);
    *width       = self->guiWidth;
    *height      = self->guiHeight;
    return true;
}

bool guiCanResize(const clap_plugin_t*) { return true; }

bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) {
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically   = true;
    hints->preserve_aspect_ratio   = false;
    hints->aspect_ratio_width      = 0;
    hints->aspect_ratio_height     = 0;
    return true;
}

// The host's units: logical on the Mac; pixels on X11 (the smallest scaled).
std::uint32_t minimum(const Plugin* self, unsigned logical) {
#ifdef __APPLE__
    (void)self;
    return logical;
#else
    return static_cast<std::uint32_t>(std::lround(logical * self->guiScale));
#endif
}

bool guiAdjustSize(const clap_plugin_t* p, std::uint32_t* width, std::uint32_t* height) {
    const Plugin* self = Plugin::self(p);
    *width             = std::max(*width, minimum(self, PluginEditor::kMinWidth));
    *height            = std::max(*height, minimum(self, PluginEditor::kMinHeight));
    return true;
}

// The window's logical size and pixels per logical pixel, from the host's units.
void windowSize(const Plugin* self, unsigned& width, unsigned& height, double& scale) {
#ifdef __APPLE__
    width  = self->guiWidth;
    height = self->guiHeight;
    scale  = self->guiScale;
#else
    scale  = self->guiScale;
    width  = static_cast<unsigned>(std::lround(self->guiWidth / scale));
    height = static_cast<unsigned>(std::lround(self->guiHeight / scale));
#endif
}

bool guiSetSize(const clap_plugin_t* p, std::uint32_t width, std::uint32_t height) {
    Plugin* self    = Plugin::self(p);
    self->guiWidth  = width;
    self->guiHeight = height;
    if (self->window) {
        unsigned w = 0, h = 0;
        double   scale = 1;
        windowSize(self, w, h, scale);
        self->window->setSize(w, h, scale);
    }
    return true;
}

bool guiSetParent(const clap_plugin_t* p, const clap_window_t* parentWindow) {
    Plugin* self = Plugin::self(p);
    if (!self->guiCreated || self->window || std::strcmp(parentWindow->api, kWindowApi) != 0) return false;
#ifdef __APPLE__
    self->guiParent = reinterpret_cast<std::uintptr_t>(parentWindow->cocoa);
    self->guiScale  = parentScale(self->guiParent);
#else
    self->guiParent = static_cast<std::uintptr_t>(parentWindow->x11);
#endif
    EditorLinks links;
    links.keeper          = &self->keeper;
    links.engine          = &self->engine;
    links.armed           = [self] { return self->armedForWindow(); };
    links.setArmed        = [self](bool on) { self->pressRecord(on); };
    links.synthName       = [self] { return self->synth ? self->synth->descriptor().name : std::string(); };
    links.synthEditorOpen = [self] { return self->synthWindow != nullptr; };
    links.toggleSynthEditor = [self] { self->toggleSynthWindow(); };
    links.synthHasEditor    = [self] { return self->synth && self->synth->hasEditor(); };
    links.inputName = [self] {
        if (self->inputMissing) return self->inputChoice.name + " (not here)";
        return self->source == uw::kInvalidMidiSource ? std::string()
                                                      : self->sourceName + (self->sourceConnected ? "" : " (gone)");
    };
    links.inputMissing = [self] { return self->inputMissing; };
    links.inputs       = [self] { return self->inputItems(); };
    links.chooseInput  = [self](const InputChoice& choice) {
        self->chooseInput(choice);
        if (self->hostState != nullptr) self->hostState->mark_dirty(self->host);
    };
    links.synthId      = [self] { return self->synth ? self->synth->descriptor().id : std::string(); };
    links.missingSynth = [self] {
        return self->synth ? std::string() : self->missingSynth.name.empty() ? self->missingSynth.id : self->missingSynth.name;
    };
    links.synths = [self] {
        std::vector<SynthItem> items;
        for (const uw::PluginDescriptor& d : self->installedSynths()) items.push_back({d.id, d.name, d.vendor});
        return items;
    };
    links.chooseSynth = [self](const std::string& id) { self->chooseSynth(id); };
    links.zoom        = &self->zoom;
    unsigned w = 0, h = 0;
    double   scale = 1;
    windowSize(self, w, h, scale);
    self->window = PluginEditor::open(self->guiParent, w, h, scale, std::move(links));
    if (!self->window) {
        self->log("the window wouldn't open");
        return false;
    }
    self->log("window opened (%ux%u at %.1f)", w, h, scale);
    self->startGuiTimer();
    return true;
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}

bool guiShow(const clap_plugin_t* p) {
    Plugin* self = Plugin::self(p);
    if (!self->window) return false;
    self->startGuiTimer();
    return true;
}

bool guiHide(const clap_plugin_t* p) {
    Plugin* self = Plugin::self(p);
    if (!self->window) return false;
    self->stopGuiTimer();
    return true;
}

const clap_plugin_gui_t kGui = {guiIsApiSupported, guiGetPreferredApi, guiCreate,       guiDestroy,
                                guiSetScale,       guiGetSize,         guiCanResize,    guiGetResizeHints,
                                guiAdjustSize,     guiSetSize,         guiSetParent,    guiSetTransient,
                                guiSuggestTitle,   guiShow,            guiHide};

const void* pluginGetExtension(const clap_plugin_t*, const char* id) {
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kAudioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &kNotePorts;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &kState;
    if (std::strcmp(id, CLAP_EXT_RENDER) == 0) return &kRender;
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &kLatency;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &kParams;
    if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0) return &kTimerSupport;
    if (std::strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT) == 0) return &kPosixFdSupport;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &kGui;
    return nullptr;
}

// --- the factory and the entry ----------------------------------------------------------

const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const clap_plugin_descriptor_t kDescriptor = {
    CLAP_VERSION_INIT,
    kOwnId,
    "Umpire",
    "Unlikelyware",
    "https://unlikelyware.com",
    "",
    "",
    UMPIRE_VERSION_STRING,
    "Records and plays MIDI 2.0 from its own input, through a synth it hosts",
    kFeatures,
};

const clap_plugin_t* factoryCreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    if (std::strcmp(id, kDescriptor.id) != 0 || !clap_version_is_compatible(host->clap_version)) return nullptr;
    auto* self = new (std::nothrow) Plugin(host);
    if (self == nullptr) return nullptr;
    clap_plugin_t& c   = self->clap;
    c.desc             = &kDescriptor;
    c.plugin_data      = self;
    c.init             = pluginInit;
    c.destroy          = pluginDestroy;
    c.activate         = pluginActivate;
    c.deactivate       = pluginDeactivate;
    c.start_processing = pluginStartProcessing;
    c.stop_processing  = pluginStopProcessing;
    c.reset            = pluginReset;
    c.process          = pluginProcess;
    c.get_extension    = pluginGetExtension;
    c.on_main_thread   = pluginOnMainThread;
    return &c;
}

std::uint32_t factoryCount(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* factoryGet(const clap_plugin_factory_t*, std::uint32_t index) {
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin_factory_t kFactory = {factoryCount, factoryGet, factoryCreate};

bool entryInit(const char*) { return true; }
void entryDeinit() {}
const void* entryGetFactory(const char* id) {
    return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
}

} // namespace
} // namespace umpire::plugin

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT,
    umpire::plugin::entryInit,
    umpire::plugin::entryDeinit,
    umpire::plugin::entryGetFactory,
};
