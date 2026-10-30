#pragma once

// The main window's NanoVG canvas: the track view's timeline (decisions.md,
// "The track view"). A bar ruler along the top and one lane per track, beside
// the ImGui track headers at the left, with the clips on the lanes and the
// playhead; the status line and the MIDI monitor take the bottom. An empty
// session shows only a prompt. The ImGui panels (TrackPanels) are its only
// subwidget, covering the window, so they draw over it and see input first.
//
// A click on the ruler moves the playhead; a drag along it sets the loop (to
// the beat) and turns looping on; L turns it on and off. A clip is selected
// by a click, moved along its lane by a drag (to the beat; Shift: freely),
// deleted with Delete, and opened in the grid by a double click.
//
// Rows line up across the two layers: both place row i at rowY(i), from the
// same scroll position, which the canvas keeps.
//
// Coordinates are logical pixels, scaled when drawing (StageHand's convention).

#include <memory>

#include "NanoVG.hpp"
#include "Window.hpp"

#include "App/TrackController.h"
#include "Ui/Probe.h"

namespace umpire::ui {

class TrackPanels;

class TrackCanvas final : public DGL_NAMESPACE::NanoTopLevelWidget {
public:
    static constexpr float kHeaderW = 390;   // the track headers' column
    static constexpr float kRowH    = 58;    // a track's row and lane
    static constexpr float kRulerH  = 24;
    static constexpr float kBeatW   = 24;    // a quarter note on the timeline

    TrackCanvas(DGL_NAMESPACE::Window& window, TrackController& controller, Probe& probe);
    ~TrackCanvas() override;

    TrackPanels& panels() noexcept { return *imgui; }

    // The layout both layers use (logical pixels).
    float sf();
    float rulerY();    // below the menu bar
    float lanesY() { return rulerY() + kRulerH; }
    float lanesH();    // down to the monitor, or the window's bottom
    float rowY(int index) { return lanesY() + static_cast<float>(index) * kRowH - scrollY; }
    int   rowAt(float y);   // -1 past the last row
    float tickX(Tick t) const { return kHeaderW + static_cast<float>(t) / kTicksPerQuarter * kBeatW - scrollX; }
    Tick  xTick(float x) const { return static_cast<Tick>((x - kHeaderW + scrollX) / kBeatW * kTicksPerQuarter); }
    // The clip under a point on a lane, or 0.
    ClipRefId clipAt(float x, float y);
    bool      dragging() const noexcept { return dragRef != 0; }
    bool      settingLoop() const noexcept { return loopDrag; }

    // A clip double-clicked: MainWindow opens its grid, then sets this back to 0.
    ClipRefId openRequested = 0;
    float scroll() const noexcept { return scrollY; }
    void  scrollBy(float rows);   // the wheel over the headers, too

    // The view model changed: draw again.
    void refresh();

protected:
    void onNanoDisplay() override;
    bool onMouse(const MouseEvent& ev) override;
    bool onMotion(const MotionEvent& ev) override;
    bool onScroll(const ScrollEvent& ev) override;
    bool onKeyboard(const KeyboardEvent& ev) override;
    void onResize(const ResizeEvent& ev) override;

private:
    friend struct TrackSelfTest;

    void clampScroll();
    void drawClip(const ClipModel& clip, float y, float laneRight);

    TrackController&             controller;
    Probe&                       probe;
    std::unique_ptr<TrackPanels> imgui;
    float                        scrollY = 0;   // logical pixels from the first row
    float                        scrollX = 0;   // logical pixels from bar 1
    // A clip being dragged: where it was grabbed, and where it would go.
    ClipRefId                    dragRef = 0;
    Tick                         dragGrab = 0, dragOrigin = 0, dragTo = 0;
    bool                         dragMoved = false;
    // A drag along the ruler: the loop it would set, from the beat pressed.
    bool                         loopDrag = false;
    Tick                         loopFrom = 0, loopTo = 0;
    ClipRefId                    lastPressRef = 0;
    std::uint32_t                lastPressTime = 0;   // ms, for a double click
};

} // namespace umpire::ui
