#pragma once

// The main window's NanoVG canvas: the track view's timeline (decisions.md,
// "The track view"). A bar ruler along the top and one lane per track, beside
// the ImGui track headers at the left; the MIDI monitor, when shown, takes the
// bottom. The ImGui panels (TrackPanels) are its only subwidget, covering the
// window, so they draw over it and see input first.
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

    TrackController&             controller;
    Probe&                       probe;
    std::unique_ptr<TrackPanels> imgui;
    float                        scrollY = 0;   // logical pixels from the first row
    float                        scrollX = 0;   // logical pixels from bar 1
};

} // namespace umpire::ui
