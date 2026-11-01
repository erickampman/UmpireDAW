#pragma once

// The main window's ImGui panels, over the track canvas: the menu bar with
// the transport, the track headers at the left (one row per track, lined up
// with the canvas's lanes), the status line, and the MIDI monitor along the
// bottom. A row shows only what isn't the default (ui-direction.md): a light,
// the name and badges; the touched track shows its controls in place. Every
// change is an intent to the TrackController.

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "App/TrackController.h"
#include "Ui/ImGuiWidget.h"
#include "Ui/Probe.h"

namespace umpire::ui {

class TrackCanvas;

class TrackPanels final : public ImGuiWidget {
public:
    TrackPanels(TrackCanvas* canvas, TrackController& controller, Probe& probe);

    // Window pixels; known after the first frame.
    float menuBarHeight() const noexcept { return menuH; }
    float statusHeight() const noexcept { return statusH; }
    float monitorHeight() const noexcept { return showMonitor ? monitorH : 0; }

    // File's commands (milestone 9, docs/files.md), from the menu or the
    // keys, for MainWindow to carry out: it asks first if there's something
    // unsaved, and runs the file dialogs.
    enum class FileAction { None, New, Open, Save, SaveAs, Quit };
    FileAction fileRequested = FileAction::None;

    // A question over the window ("Save changes to "Song"?"), its buttons in
    // order, the first the default (Enter), the last Cancel (Escape) when
    // there's more than one. Set by MainWindow; once a button is pressed,
    // `promptAnswer` is its index and `prompt` is cleared.
    struct Prompt {
        std::string              text;
        std::vector<std::string> buttons;
    };
    std::optional<Prompt> prompt;
    int                   promptAnswer = -1;

    bool showMonitor   = true;
    bool showDiagnostics = false;
    bool showDemo      = false;

    // For the self-test: where things were drawn in the last frame (window pixels).
    struct Rect {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        float cx() const { return (x0 + x1) / 2; }
        float cy() const { return (y0 + y1) / 2; }
    };
    struct RowRects {
        TrackId id = 0;
        Rect    row, name, arm, mute;   // arm and mute: the touched row's only
    };
    std::vector<RowRects> rowRects;
    Rect addTrackRect, trackMenuRect, editMenuRect, playRect, recordRect, loopRect, tempoRect, statusRect, monitorRect;
    Rect outputRect;   // the touched row's output choice

protected:
    void onImGuiDisplay() override;
    void onStyle(ImGuiStyle& style) override;
    void routed(Input input, bool toImGui) override;

private:
    void menuBar();
    void transport();
    void headers();
    void row(const TrackRowModel& row, float x, float y, float w);
    void status();
    void monitor();
    void promptPopup();

    TrackCanvas&                              canvas;
    TrackController&                          controller;
    Probe&                                    probe;
    float                                     menuH    = 0;
    float                                     statusH  = 0;
    float                                     monitorH = 0;
    std::map<TrackId, std::array<char, 64>>   names;     // the name fields' text
    TrackId                                   editing = 0;   // the name being typed, kept as typed
    float                                     tempoEdit = 0;   // the tempo while it's dragged
    bool                                      tempoActive = false;
};

} // namespace umpire::ui
