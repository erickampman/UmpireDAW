#pragma once

// The plugin's own window (milestone P3, docs/plugin-version.md, "P3"),
// embedded in the host's plugin window: a header (Record, Edit, the synth's
// editor, what's listening, the host's position), the lane of takes in time
// with the host, and the grid for the selected take (the app's GridCanvas and
// GridPanels, src/Ui). No DGL or ImGui types here: UmpirePlugin.cpp, the CLAP
// glue, doesn't see the toolkit.
//
// Main thread only. It reaches the lane through LaneKeeper (its edits, its
// undo) and reads the host's position from the engine's atomics; the audio
// thread never waits for it.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace umpire::plugin {

class LaneKeeper;
class PluginEngine;

// What the window reaches in the plugin.
struct EditorLinks {
    LaneKeeper*   keeper = nullptr;
    PluginEngine* engine = nullptr;
    std::function<bool()>        armed;      // the Record parameter, as it is (or as just pressed)
    std::function<void(bool)>    setArmed;   // the Record button: to the host, as a gesture
    std::function<std::string()> synthName;  // "" with no synth
    std::function<bool()>        synthEditorOpen;
    std::function<void()>        toggleSynthEditor;   // unset: the synth has no editor
    std::function<std::string()> inputName;  // "" with no input
};

class PluginEditor {
public:
    // Its size when it first opens, and the smallest, in logical pixels
    // (points on the Mac).
    static constexpr unsigned kWidth = 960, kHeight = 600, kMinWidth = 640, kMinHeight = 400;

    // The window inside `parent` (an NSView on the Mac, an X11 window on
    // Linux), `width` by `height` logical pixels at `scale` pixels each.
    // Null if it couldn't open.
    static std::unique_ptr<PluginEditor> open(std::uintptr_t parent, unsigned width, unsigned height, double scale,
                                              EditorLinks links);
    ~PluginEditor();

    PluginEditor(const PluginEditor&)            = delete;
    PluginEditor& operator=(const PluginEditor&) = delete;

    // From the host's timer, about 60 times a second: follows the lane and
    // the host's position, then DGL's events and drawing.
    void idle();
    void setSize(unsigned width, unsigned height, double scale);

private:
    struct Impl;
    explicit PluginEditor(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl;
};

// The parent view's backing scale now (the Mac: from its window's screen;
// elsewhere 1). DGL reads it once, maybe from the wrong screen, so the plugin
// sizes from this (as Retune's editor does).
double parentScale(std::uintptr_t parent);

} // namespace umpire::plugin
