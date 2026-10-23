#pragma once

// The main window's ImGui panels, over the track canvas: the menu bar, the
// track headers at the left (one row per track, lined up with the canvas's
// lanes), and the MIDI monitor along the bottom. The headers are StageHand's
// rack rows in Umpire's look: an activity light, the name, arm, mute and solo,
// then where the track's MIDI comes from (input, channel, group) and where it
// goes. Every change is an intent to the TrackController.

#include <array>
#include <map>
#include <vector>

#include "App/TrackController.h"
#include "Ui/ImGuiWidget.h"
#include "Ui/Probe.h"

namespace umpire::ui {

class TrackCanvas;

class TrackPanels final : public ImGuiWidget {
public:
    TrackPanels(TrackCanvas* canvas, TrackController& controller, Probe& probe);

    float menuBarHeight() const noexcept { return menuH; }   // window pixels; known after the first frame
    float monitorHeight() const noexcept { return showMonitor ? monitorH : 0; }

    bool quitRequested = false;
    bool gridRequested = false;   // View > Test grid window: MainWindow opens it
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
        Rect    row, name, arm, mute, input;
    };
    std::vector<RowRects> rowRects;
    Rect                  addTrackRect, trackMenuRect, monitorRect;

protected:
    void onImGuiDisplay() override;
    void onStyle(ImGuiStyle& style) override;
    void routed(Input input, bool toImGui) override;

private:
    void menuBar();
    void headers();
    void row(const TrackRowModel& row, float x, float y, float w);
    void monitor();

    TrackCanvas&                              canvas;
    TrackController&                          controller;
    Probe&                                    probe;
    float                                     menuH    = 0;
    float                                     monitorH = 0;
    std::map<TrackId, std::array<char, 64>>   names;     // the name fields' text
    TrackId                                   editing = 0;   // the name being typed, kept as typed
};

} // namespace umpire::ui
