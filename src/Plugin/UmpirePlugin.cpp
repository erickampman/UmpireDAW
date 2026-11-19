// Umpire as a CLAP instrument: the spike (milestone P1, docs/plugin-version.md).
//
// Our own CLAP entry point, as Retune's (no DPF wrapper: DPF has no recording
// flag and doesn't pass on timer-support or posix-fd-support), wrapped as a
// VST3 by clap-wrapper for Ardour. The notes never pass through the host:
//   - the plugin opens one MIDI input itself, through uw-core (CoreMIDI on
//     the Mac, the ALSA sequencer on Linux), and takes no MIDI from the host;
//   - it hosts one CLAP synth through uw-core's CLAP host (uw::ClapPlugin)
//     and runs it inside its own process() (Plugin/SpikeEngine);
//   - it records while the host records and plays back from the host's
//     position; the host hears only audio.
//
// No editor of its own: its editor is the synth's, embedded in the host's
// window (CLAP gui, forwarded to uw::HostedPlugin::openEditor). What it
// does goes to a log file (logPath() below), since there's nothing else to
// show it on: the input it opened, takes, the live input's timing, the
// state, and a heartbeat counting the host's timer ticks and main-thread
// callbacks, which is how the spike checks that timer-support keeps working
// with the editor closed (check 7). Setup comes from a small text file
// (configPath(), Plugin/PluginSetup.h).

#include <clap/clap.h>

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#include "App/ClipFile.h"
#include "Plugin/PluginSetup.h"
#include "Plugin/SpikeEngine.h"
#include "uw/base/Clock.h"
#include "uw/clap/ClapPluginFactory.h"
#include "uw/midi/PlatformMidiInput.h"
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
    uw::TimerId  addTimer(std::uint32_t periodMs, std::function<void()> callback) override;
    void         removeTimer(uw::TimerId id) override;
    bool watchFd(int fd, std::uint32_t events, std::function<void(std::uint32_t)> callback) override;
    bool modifyFd(int fd, std::uint32_t events) override;
    void unwatchFd(int fd) override;

private:
    Plugin& plugin;
};

// The synth's editor, embedded in the host's window: its requests go to the
// host's CLAP gui.
class EditorBridge final : public uw::EditorHost {
public:
    explicit EditorBridge(Plugin& p) : plugin(p) {}
    bool requestEditorResize(uw::EditorSize size) override;
    bool requestEditorShown(bool shown) override;
    void editorResizeHintsChanged() override;
    void editorClosed() override;

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
    const int                         instance;

    Config         config;
    ForwardingHost services{*this};
    EditorBridge   bridge{*this};
    SpikeEngine    engine;

    std::shared_ptr<uw::ClapPluginFactory> factory;
    std::unique_ptr<uw::HostedPlugin>      synth;

    std::unique_ptr<uw::PlatformMidiInput> midi;
    uw::MidiSourceHandle                   source = uw::kInvalidMidiSource;
    std::string                            sourceName;
    std::uint64_t                          seenChanges = 0;
    bool                                   sourceConnected = false;

    Clip clip;   // main thread: the clip, as the state saves it

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
    bool          activeOnce   = false;
    double        callbackFrames = 2400;   // a main-thread callback asked for every 50 ms of audio
    double        framesSinceCallback = 0;

    // The editor (the synth's).
    bool           editorOpen = false;
    uw::EditorSize lastSize{640, 480};

    void log(const char* format, ...) __attribute__((format(printf, 2, 3)));

    bool init();
    void destroy();
    void openInput();
    void housekeeping();
    void report();

    static Plugin* self(const clap_plugin_t* p) { return static_cast<Plugin*>(p->plugin_data); }
    static void    requestCallback(void* context) {
        auto* p = static_cast<Plugin*>(context);
        p->host->request_callback(p->host);
    }
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

bool EditorBridge::requestEditorResize(uw::EditorSize size) {
    if (plugin.hostGui == nullptr) return false;
    plugin.lastSize = size;
    return plugin.hostGui->request_resize(plugin.host, size.width, size.height);
}

bool EditorBridge::requestEditorShown(bool shown) {
    if (plugin.hostGui == nullptr) return false;
    return shown ? plugin.hostGui->request_show(plugin.host) : plugin.hostGui->request_hide(plugin.host);
}

void EditorBridge::editorResizeHintsChanged() {
    if (plugin.hostGui != nullptr) plugin.hostGui->resize_hints_changed(plugin.host);
}

void EditorBridge::editorClosed() {
    plugin.editorOpen = false;
    if (plugin.hostGui != nullptr) plugin.hostGui->closed(plugin.host, true);
}

// --- life -------------------------------------------------------------------------------

bool Plugin::init() {
    hostLog   = static_cast<const clap_host_log_t*>(host->get_extension(host, CLAP_EXT_LOG));
    hostTimer = static_cast<const clap_host_timer_support_t*>(host->get_extension(host, CLAP_EXT_TIMER_SUPPORT));
    hostFd    = static_cast<const clap_host_posix_fd_support_t*>(host->get_extension(host, CLAP_EXT_POSIX_FD_SUPPORT));
    hostGui   = static_cast<const clap_host_gui_t*>(host->get_extension(host, CLAP_EXT_GUI));
    hostState = static_cast<const clap_host_state_t*>(host->get_extension(host, CLAP_EXT_STATE));

    const fs::path confPath = configPath();
    config = parseConfig(readFile(confPath));
    log("Umpire %s in %s %s; setup from %s: input '%s', synth '%s'", UMPIRE_VERSION_STRING,
        host->name ? host->name : "?", host->version ? host->version : "?", confPath.c_str(),
        config.input.empty() ? "(choose)" : config.input.c_str(), config.synth.c_str());
    log("host gives timer-support: %s, posix-fd-support: %s, gui: %s", hostTimer ? "yes" : "no",
        hostFd ? "yes" : "no", hostGui ? "yes" : "no");

    if (config.synth != "none") {
        factory = sharedFactory();
        const std::string path = findClap(factory->searchPaths(), config.synth);
        if (path.empty()) {
            log("no %s.clap in CLAP's folders: no synth", config.synth.c_str());
        } else {
            const std::vector<uw::PluginDescriptor> found = factory->scanFile(path);
            const uw::PluginDescriptor*             desc  = nullptr;
            for (const auto& d : found)
                if (config.synthId.empty() || d.id == config.synthId) {
                    desc = &d;
                    break;
                }
            if (desc == nullptr) log("%s has no plugin '%s'", path.c_str(), config.synthId.c_str());
            else if (desc->id == "com.unlikelyware.umpire") log("won't host itself");
            else {
                synth = factory->create(*desc, services);
                log(synth ? "synth: %s (%s), from %s" : "synth %s (%s) wouldn't load, from %s", desc->name.c_str(),
                    desc->id.c_str(), path.c_str());
            }
        }
    }
    engine.setSynth(synth.get());
    engine.setNotify(&Plugin::requestCallback, this);

    if (config.input != "none") {
        midi = uw::PlatformMidiInput::create("Umpire");
        if (midi) {
            midi->setSink(&engine);
            seenChanges = midi->changes();
            openInput();
        } else {
            log("couldn't reach %s", "the MIDI system");
        }
    }

    if (hostTimer != nullptr && !hostTimer->register_timer(host, 50, &ownTimer)) ownTimer = CLAP_INVALID_ID;
    return true;
}

void Plugin::destroy() {
    if (midi) {   // no more input before the engine goes
        if (source != uw::kInvalidMidiSource) midi->close(source);
        midi.reset();
    }
    if (synth && editorOpen) synth->closeEditor();
    engine.deactivate();   // the host should have already; harmless if so
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

void Plugin::openInput() {
    nextInputTry = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    const std::vector<uw::MidiSourceInfo> sources = midi->enumerate();
    const int                             chosen  = chooseSource(sources, config.input);
    if (chosen < 0) {
        if (sourceName != "-") {   // said once, not every retry
            std::string names;
            for (const auto& s : sources) names += (names.empty() ? "" : ", ") + s.name;
            log("no input to open (wanted '%s'); there are: %s", config.input.c_str(), names.empty() ? "none" : names.c_str());
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
    log("input: '%s' (%s), %s", info.name.c_str(), info.id.c_str(), midi->describe(source).c_str());
}

// The main thread's work, from our timer and from main-thread callbacks.
void Plugin::housekeeping() {
    if (synth) synth->idle();

    Clip take;
    Tick takeFrom = 0;
    if (engine.collectTake(take, &takeFrom)) {
        const auto notes = notesOf(take.events, take.length);
        clip             = std::move(take);
        engine.setClip(clip);
        const auto beats = [](Tick t) { return static_cast<double>(t) / static_cast<double>(kTicksPerQuarter); };
        Tick firstOn = -1;
        for (const ClipEvent& e : clip.events)
            if (uw::ump::isNoteOn(e.message)) { firstOn = e.tick; break; }
        log("take: %zu notes, %zu events, from beat %.2f to beat %.2f, first note-on at beat %.2f, at %.1f bpm",
            notes.size(), clip.events.size(), beats(takeFrom), beats(clip.length),
            firstOn < 0 ? -1.0 : beats(firstOn), engine.lastTempo());
        if (hostState != nullptr) hostState->mark_dirty(host);
    }
    engine.collectGarbage();

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

// A heartbeat every 10 s: what the main thread has been given, and the live
// input's timing.
void Plugin::report() {
    lastReport = std::chrono::steady_clock::now();
    if (!activeOnce) return;
    log("heartbeat: %llu timer ticks, %llu main-thread callbacks, %llu synth timer ticks; editor %s; %s",
        static_cast<unsigned long long>(timerTicks), static_cast<unsigned long long>(callbacks),
        static_cast<unsigned long long>(synthTimerTicks), editorOpen ? "open" : "closed",
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
    Plugin* self = Plugin::self(p);
    if (!self->engine.activate(sampleRate, maxFrames)) self->log("the synth wouldn't activate: silence");
    self->callbackFrames      = sampleRate * 0.05;
    self->framesSinceCallback = 0;
    self->activeOnce          = true;
    self->log("active at %.0f Hz, blocks up to %u; the synth takes %s", sampleRate, maxFrames,
              !self->engine.synthHasNotes() ? "no notes" : self->engine.synthTakesMidi2() ? "MIDI 2.0" : "only MIDI 1.0");
    return true;
}

void pluginDeactivate(const clap_plugin_t* p) { Plugin::self(p)->engine.deactivate(); }
bool pluginStartProcessing(const clap_plugin_t*) { return true; }
void pluginStopProcessing(const clap_plugin_t* p) { Plugin::self(p)->engine.stopProcessing(); }
void pluginReset(const clap_plugin_t* p) { Plugin::self(p)->engine.reset(); }

clap_process_status pluginProcess(const clap_plugin_t* p, const clap_process_t* process) {
    Plugin* self = Plugin::self(p);
    Block   b;
    b.frames = process->frames_count;
    b.now    = uw::nowNanoseconds();
    if (const clap_event_transport_t* t = process->transport) {
        b.transport.playing   = (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
        b.transport.recording = (t->flags & CLAP_TRANSPORT_IS_RECORDING) != 0 &&
                                (t->flags & CLAP_TRANSPORT_IS_WITHIN_PRE_ROLL) == 0;
        b.transport.hasBeats  = (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0;
        b.transport.songBeats = static_cast<double>(t->song_pos_beats) / CLAP_BEATTIME_FACTOR;
        b.transport.barStartBeats = static_cast<double>(t->bar_start) / CLAP_BEATTIME_FACTOR;
        b.transport.barNumber     = t->bar_number;
        if ((t->flags & CLAP_TRANSPORT_HAS_TEMPO) != 0) b.transport.tempo = t->tempo;
        if ((t->flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE) != 0) {
            b.transport.tsigNum = t->tsig_num;
            b.transport.tsigDen = t->tsig_denom;
        }
    }
    if (process->audio_outputs_count > 0 && process->audio_outputs[0].data32 != nullptr) {
        b.out         = process->audio_outputs[0].data32;
        b.outChannels = process->audio_outputs[0].channel_count;
        process->audio_outputs[0].constant_mask = 0;
    }
    self->engine.process(b);

    // The main thread is asked in now and then, so the synth's callbacks and
    // a finished take are seen to on hosts without timer-support.
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

// --- state: the clip as a MIDI Clip File, and the synth's own ---------------------------

bool stateSave(const clap_plugin_t* p, const clap_ostream_t* stream) {
    Plugin* self = Plugin::self(p);
    self->housekeeping();   // a take just finished goes in
    SavedState saved;
    if (!self->clip.events.empty() || self->clip.length > 0)
        saved.clipFile = clipfile::write(self->clip.events, self->clip.length,
                                         clipfile::Tempo{self->engine.lastTempo(), self->engine.lastBeatsPerBar()});
    if (self->synth) {
        saved.synthId = self->synth->descriptor().id;
        if (!self->synth->saveState(saved.synthState)) saved.synthState.clear();
    }
    const std::vector<std::uint8_t> bytes = encodeState(saved);
    std::size_t                     at    = 0;
    while (at < bytes.size()) {
        const std::int64_t n = stream->write(stream, bytes.data() + at, bytes.size() - at);
        if (n <= 0) return false;
        at += static_cast<std::size_t>(n);
    }
    self->log("saved: clip %zu bytes, synth state %zu bytes", saved.clipFile.size(), saved.synthState.size());
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
    Clip loaded;
    if (!saved.clipFile.empty()) {
        const clipfile::Read read = clipfile::read(saved.clipFile.data(), saved.clipFile.size());
        if (!read.ok) {
            self->log("couldn't read the saved clip: %s", read.error.c_str());
            return false;
        }
        loaded.name   = "Take";
        loaded.events = read.events;
        loaded.length = read.length;
    }
    self->clip = std::move(loaded);
    self->engine.setClip(self->clip);

    bool synthLoaded = false;
    if (self->synth && !saved.synthState.empty()) {
        if (saved.synthId != self->synth->descriptor().id)
            self->log("the state's synth is %s, not %s: its state isn't loaded", saved.synthId.c_str(),
                      self->synth->descriptor().id.c_str());
        else synthLoaded = self->synth->loadState(saved.synthState.data(), saved.synthState.size());
    }
    self->log("loaded: clip of %zu events to beat %.2f; synth state %s", self->clip.events.size(),
              static_cast<double>(self->clip.length) / static_cast<double>(kTicksPerQuarter),
              synthLoaded ? "loaded" : "not loaded");
    return true;
}

const clap_plugin_state_t kState = {stateSave, stateLoad};

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

// --- gui: the synth's editor, embedded -------------------------------------------------

bool guiIsApiSupported(const clap_plugin_t* p, const char* api, bool isFloating) {
    Plugin* self = Plugin::self(p);
    return !isFloating && std::strcmp(api, kWindowApi) == 0 && self->synth && self->synth->hasEditor();
}

bool guiGetPreferredApi(const clap_plugin_t* p, const char** api, bool* isFloating) {
    Plugin* self = Plugin::self(p);
    if (!self->synth || !self->synth->hasEditor()) return false;
    *api        = kWindowApi;
    *isFloating = false;
    return true;
}

bool guiCreate(const clap_plugin_t* p, const char* api, bool isFloating) {
    return guiIsApiSupported(p, api, isFloating);
}

void guiDestroy(const clap_plugin_t* p) {
    Plugin* self = Plugin::self(p);
    if (self->editorOpen) self->synth->closeEditor();
    if (self->editorOpen) self->log("editor closed");
    self->editorOpen = false;
}

bool guiSetScale(const clap_plugin_t*, double) { return false; }

bool guiGetSize(const clap_plugin_t* p, std::uint32_t* width, std::uint32_t* height) {
    Plugin* self = Plugin::self(p);
    *width       = self->lastSize.width;
    *height      = self->lastSize.height;
    return true;
}

bool guiCanResize(const clap_plugin_t* p) {
    Plugin* self = Plugin::self(p);
    return self->editorOpen && self->synth->editorResizable();
}

bool guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
bool guiAdjustSize(const clap_plugin_t* p, std::uint32_t*, std::uint32_t*) { return guiCanResize(p); }

bool guiSetSize(const clap_plugin_t* p, std::uint32_t width, std::uint32_t height) {
    Plugin*        self = Plugin::self(p);
    uw::EditorSize size{width, height};
    if (!self->editorOpen || !self->synth->setEditorSize(size)) return false;
    self->lastSize = size;
    return true;
}

bool guiSetParent(const clap_plugin_t* p, const clap_window_t* window) {
    Plugin* self = Plugin::self(p);
    if (!self->synth || self->editorOpen) return false;
#ifdef __APPLE__
    const auto parent = reinterpret_cast<uw::NativeWindow>(window->cocoa);
#else
    const auto parent = static_cast<uw::NativeWindow>(window->x11);
#endif
    uw::EditorSize size;
    if (!self->synth->openEditor(parent, self->bridge, size)) {
        self->log("the synth's editor wouldn't open");
        return false;
    }
    self->editorOpen = true;
    self->log("editor opened (%ux%u)", size.width, size.height);
    if (size.width != self->lastSize.width || size.height != self->lastSize.height) {
        self->lastSize = size;
        if (self->hostGui != nullptr) self->hostGui->request_resize(self->host, size.width, size.height);
    }
    return true;
}

bool guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool guiShow(const clap_plugin_t*) { return true; }
bool guiHide(const clap_plugin_t*) { return true; }

const clap_plugin_gui_t kGui = {guiIsApiSupported, guiGetPreferredApi, guiCreate,       guiDestroy,
                                guiSetScale,       guiGetSize,         guiCanResize,    guiGetResizeHints,
                                guiAdjustSize,     guiSetSize,         guiSetParent,    guiSetTransient,
                                guiSuggestTitle,   guiShow,            guiHide};

const void* pluginGetExtension(const clap_plugin_t*, const char* id) {
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kAudioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &kNotePorts;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &kState;
    if (std::strcmp(id, CLAP_EXT_RENDER) == 0) return &kRender;
    if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0) return &kTimerSupport;
    if (std::strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT) == 0) return &kPosixFdSupport;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &kGui;
    return nullptr;
}

// --- the factory and the entry ----------------------------------------------------------

const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const clap_plugin_descriptor_t kDescriptor = {
    CLAP_VERSION_INIT,
    "com.unlikelyware.umpire",
    "Umpire",
    "Unlikelyware",
    "https://unlikelyware.com",
    "",
    "",
    UMPIRE_VERSION_STRING,
    "Records and plays MIDI 2.0 from its own input, through a synth it hosts (a spike)",
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
