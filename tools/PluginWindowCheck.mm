// A check of the plugin's window (milestone P3, docs/plugin-version.md),
// on the Mac, by eye: a bare Cocoa host loads the built Umpire.clap (no synth,
// no MIDI input: its setup file says none, so it's silent), loads three
// takes through the state, plays its transport at 120 bpm, opens the
// plugin's window in an NSWindow, and then clicks into it itself (NSEvents
// sent to its own window, so no permissions): a take, the Record button, the
// grid (a note added), Undo. It writes a screenshot after each step
// (screencapture -l, of its window only) and says what the plugin did:
// the Record gestures it sent the host, and the take's notes in the saved
// state after the edit and after the undo.
//
//   UMPIRE_PLUGIN_CONF=build/test-plugin.conf UMPIRE_PLUGIN_LOG=none \
//       UMPIRE_PLUGIN_CAPTURE=DIR build/umpire-plugin-window-check build/Umpire.clap DIR
//
// screencapture needs the Screen Recording permission; without it, the
// window's own frames (UMPIRE_PLUGIN_CAPTURE, one a second, frame-N.ppm)
// show the same steps.
//
// Not a ctest: it opens a window. Built only when asked
// (ninja -C build umpire-plugin-window-check).

#import <Cocoa/Cocoa.h>

#include <clap/clap.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "App/ClipFile.h"
#include "Plugin/PluginSetup.h"
#include "uw/ump/Ump.h"

using namespace umpire;
namespace ump = uw::ump;

namespace {

int failures = 0;
void check(const char* what, bool ok, const std::string& detail = {}) {
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str());
    if (!ok) ++failures;
}

bool flushWanted = false, callbackWanted = false;
void hostRequestFlush(const clap_host_t*) { flushWanted = true; }
void hostRescan(const clap_host_t*, clap_param_rescan_flags) {}
void hostClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
const clap_host_params_t kHostParams = {hostRescan, hostClear, hostRequestFlush};
bool hostRequestResize(const clap_host_t*, std::uint32_t, std::uint32_t) { return true; }
bool hostRequestShowHide(const clap_host_t*) { return true; }
void hostHintsChanged(const clap_host_t*) {}
void hostGuiClosed(const clap_host_t*, bool) {}
const clap_host_gui_t kHostGui = {hostHintsChanged, hostRequestResize, hostRequestShowHide, hostRequestShowHide,
                                  hostGuiClosed};

// No timer-support, as Bitwig on the Mac: the window runs its own timer.
const void* hostGetExtension(const clap_host_t*, const char* id) {
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &kHostParams;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &kHostGui;
    return nullptr;
}
void hostRequestRestart(const clap_host_t*) {}
void hostRequestProcess(const clap_host_t*) {}
void hostRequestCallback(const clap_host_t*) { callbackWanted = true; }
const clap_host_t kHost = {CLAP_VERSION_INIT, nullptr, "umpire-plugin-window-check", "Unlikelyware", "", "0",
                           hostGetExtension, hostRequestRestart, hostRequestProcess, hostRequestCallback};

struct Reader {
    const std::vector<std::uint8_t>* bytes;
    std::size_t                      at = 0;
};
std::int64_t readStream(const clap_istream_t* s, void* buffer, std::uint64_t size) {
    auto*      r = static_cast<Reader*>(s->ctx);
    const auto n = std::min<std::uint64_t>(size, r->bytes->size() - r->at);
    std::memcpy(buffer, r->bytes->data() + r->at, n);
    r->at += n;
    return static_cast<std::int64_t>(n);
}
std::int64_t writeStream(const clap_ostream_t* s, const void* buffer, std::uint64_t size) {
    auto* out = static_cast<std::vector<std::uint8_t>*>(s->ctx);
    out->insert(out->end(), static_cast<const std::uint8_t*>(buffer), static_cast<const std::uint8_t*>(buffer) + size);
    return static_cast<std::int64_t>(size);
}

Tick beats(double b) { return std::llround(b * kTicksPerQuarter); }

// A take: notes of a quarter, rising from `note`, every half beat.
plugin::SavedTake take(Tick start, double lengthBeats, int note, int count) {
    std::vector<ClipEvent> events;
    for (int i = 0; i < count; ++i) {
        events.push_back({beats(0.5 * i), ump::midi2NoteOn(0, 0, static_cast<std::uint8_t>(note + i % 7), 0x9000)});
        events.push_back({beats(0.5 * i + 0.25), ump::midi2NoteOff(0, 0, static_cast<std::uint8_t>(note + i % 7))});
    }
    std::stable_sort(events.begin(), events.end(), [](const ClipEvent& a, const ClipEvent& b) { return a.tick < b.tick; });
    return plugin::savedTake(start, clipfile::write(events, beats(lengthBeats), {120, 4}));
}

std::vector<std::uint32_t> armEvents;   // what the plugin told the host: 1 begin, 2 value on, 3 value off, 4 end
bool pushOut(const clap_output_events_t*, const clap_event_header_t* h) {
    if (h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN) armEvents.push_back(1);
    if (h->type == CLAP_EVENT_PARAM_GESTURE_END) armEvents.push_back(4);
    if (h->type == CLAP_EVENT_PARAM_VALUE)
        armEvents.push_back(reinterpret_cast<const clap_event_param_value_t*>(h)->value >= 0.5 ? 2 : 3);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s Umpire.clap DIR\n", argv[0]);
        return 2;
    }
    const std::string bundle = argv[1], dir = argv[2];
    void* lib = dlopen((bundle + "/Contents/MacOS/Umpire").c_str(), RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr) {
        std::fprintf(stderr, "can't load %s: %s\n", bundle.c_str(), dlerror());
        return 2;
    }
    const auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
    entry->init(bundle.c_str());
    const auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin_t* p = factory->create_plugin(factory, &kHost, "com.unlikelyware.umpire");
    if (p == nullptr || !p->init(p)) return 2;
    const auto* state  = static_cast<const clap_plugin_state_t*>(p->get_extension(p, CLAP_EXT_STATE));
    const auto* gui    = static_cast<const clap_plugin_gui_t*>(p->get_extension(p, CLAP_EXT_GUI));
    const auto* params = static_cast<const clap_plugin_params_t*>(p->get_extension(p, CLAP_EXT_PARAMS));

    // Three takes: Take 1, beats 0 to 8; Take 2, beats 4 to 6 (covering part
    // of 1); Take 3, beats 12 to 16.
    plugin::SavedState saved;
    saved.takes = {take(0, 8, 60, 16), take(beats(4), 2, 67, 4), take(beats(12), 4, 64, 8)};
    const std::vector<std::uint8_t> bytes = plugin::encodeState(saved);
    {
        Reader         r{&bytes};
        clap_istream_t s{&r, readStream};
        check("three takes load", state->load(p, &s));
    }
    p->activate(p, 48000, 1, 512);
    p->start_processing(p);

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    std::uint32_t w = 960, h = 600;
    gui->create(p, CLAP_WINDOW_API_COCOA, false);
    gui->get_size(p, &w, &h);
    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(80, 80, w, h)
                                                   styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskResizable
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    [window setTitle:@"Umpire window check"];
    clap_window_t parent{};
    parent.api   = CLAP_WINDOW_API_COCOA;
    parent.cocoa = (__bridge void*)[window contentView];
    check("the window opens", gui->set_parent(p, &parent));
    gui->show(p);
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    // The host's process(), on the main thread, playing from beat 0 at 120.
    __block double beat = 0;
    __block std::int64_t steady = 0;
    NSTimer* audio = [NSTimer scheduledTimerWithTimeInterval:512.0 / 48000 repeats:YES block:^(NSTimer*) {
        std::vector<float> left(512), right(512);
        float*             channels[2] = {left.data(), right.data()};
        clap_audio_buffer_t buffer{};
        buffer.data32        = channels;
        buffer.channel_count = 2;
        clap_input_events_t  in{nullptr, [](const clap_input_events_t*) -> std::uint32_t { return 0; },
                               [](const clap_input_events_t*, std::uint32_t) -> const clap_event_header_t* { return nullptr; }};
        clap_output_events_t out{nullptr, pushOut};
        clap_event_transport_t t{};
        t.header.size    = sizeof t;
        t.header.type    = CLAP_EVENT_TRANSPORT;
        t.flags          = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING |
                  CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
        t.tempo          = 120;
        t.tsig_num       = 4;
        t.tsig_denom     = 4;
        t.song_pos_beats = static_cast<clap_beattime>(std::llround(beat * CLAP_BEATTIME_FACTOR));
        clap_process_t pr{};
        pr.steady_time         = steady;
        pr.frames_count        = 512;
        pr.transport           = &t;
        pr.audio_outputs       = &buffer;
        pr.audio_outputs_count = 1;
        pr.in_events           = &in;
        pr.out_events          = &out;
        p->process(p, &pr);
        steady += 512;
        beat += 512.0 / 48000 * 2;
        if (callbackWanted) {
            callbackWanted = false;
            p->on_main_thread(p);
        }
        flushWanted = false;   // process() just did it
    }];
    (void)audio;
    (void)params;

    const auto shot = [&](const char* name) {
        const std::string path = dir + "/" + name + ".png";
        const std::string cmd  = "screencapture -x -o -l " + std::to_string([window windowNumber]) + " '" + path + "'";
        const int         rc   = std::system(cmd.c_str());
        std::printf("%s  %s\n", rc == 0 ? "shot" : "no shot", path.c_str());
    };
    // A click at (x, y) points from the content's top left.
    const auto click = [&](double x, double y, bool right = false) {
        const double   H  = [[window contentView] frame].size.height;
        const NSPoint  at = NSMakePoint(x, H - y);
        const NSEventType down = right ? NSEventTypeRightMouseDown : NSEventTypeLeftMouseDown;
        const NSEventType up   = right ? NSEventTypeRightMouseUp : NSEventTypeLeftMouseUp;
        for (NSEventType type : {down, up}) {
            NSEvent* e = [NSEvent mouseEventWithType:type location:at modifierFlags:0 timestamp:0
                                        windowNumber:[window windowNumber] context:nil eventNumber:0
                                          clickCount:1 pressure:type == down ? 1.0f : 0.0f];
            [window sendEvent:e];
        }
    };
    const auto key = [&](NSString* chars, NSEventModifierFlags mods) {
        for (NSEventType type : {NSEventTypeKeyDown, NSEventTypeKeyUp}) {
            NSEvent* e = [NSEvent keyEventWithType:type location:NSZeroPoint modifierFlags:mods timestamp:0
                                      windowNumber:[window windowNumber] context:nil characters:chars
                                charactersIgnoringModifiers:chars isARepeat:NO keyCode:6];
            [window sendEvent:e];
        }
    };
    const auto notesOfTake = [&](std::size_t index) {
        std::vector<std::uint8_t> out;
        clap_ostream_t            s{&out, writeStream};
        state->save(p, &s);
        plugin::SavedState back;
        std::string        error;
        if (!plugin::decodeState(out.data(), out.size(), back, error) || index >= back.takes.size()) return -1;
        const clipfile::Read r = clipfile::read(back.takes[index].clipFile.data(), back.takes[index].clipFile.size());
        return r.ok ? static_cast<int>(notesOf(r.events, r.length).size()) : -1;
    };

    // The steps, a second apart: lane alone; Take 1 selected; Record on; a
    // note added in the grid; undone.
    std::vector<std::function<void()>> steps = {
        [&] { shot("1-lane"); },
        [&] { click(30, 62); },   // Take 1, at beat 1 in the lane (the ruler 16, a gap, the takes 40)
        [&] { shot("2-take-selected"); },
        [&] { click(40, 11); },   // Record, first in the header
        [&] {
            shot("3-record-on");
            check("the Record button tells the host: a gesture, On",
                  armEvents.size() == 3 && armEvents[0] == 1 && armEvents[1] == 2 && armEvents[2] == 4,
                  std::to_string(armEvents.size()) + " events");
            double v = 0;
            params->get_value(p, 1, &v);
            check("and Record is on", v == 1.0);
        },
        [&] { click(300, 400); },   // empty grid: a note added
        [&] {
            shot("4-note-added");
            check("the grid's note goes into the take", notesOfTake(0) == 17, std::to_string(notesOfTake(0)));
            key(@"z", NSEventModifierFlagCommand);
        },
        [&] {
            shot("5-undone");
            check("Cmd+Z undoes it, through the lane", notesOfTake(0) == 16, std::to_string(notesOfTake(0)));
        },
        [&] {
            gui->destroy(p);
            p->stop_processing(p);
            p->deactivate(p);
            p->destroy(p);
            entry->deinit();
            std::printf("%s\n", failures == 0 ? "all passed" : "FAILURES");
            std::exit(failures == 0 ? 0 : 1);
        },
    };
    __block std::size_t next = 0;
    [NSTimer scheduledTimerWithTimeInterval:1.0 repeats:YES block:^(NSTimer*) {
        if (next < steps.size()) steps[next++]();
    }];
    [NSApp run];
    return 0;
}
