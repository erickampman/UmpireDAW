#pragma once

// The NanoVG canvas: the window's top-level widget, drawing a note grid (rows
// are pitches, columns sixteenths) as StageHand's windows draw (UiWidget). The
// ImGui panels are its only subwidget, covering the window, so they draw over
// the canvas and see input first; the canvas handles what they pass on
// (ImGuiWidget.h has the rules).
//
// Coordinates are logical pixels; the window's scale factor is applied when
// drawing (StageHand's convention). ImGui works in window pixels.

#include <memory>

#include "NanoVG.hpp"
#include "Window.hpp"

#include "App/GridModel.h"
#include "Ui/Probe.h"

namespace umpire::ui {

class GridPanels;

class GridCanvas final : public DGL_NAMESPACE::NanoTopLevelWidget {
public:
    GridCanvas(DGL_NAMESPACE::Window& window, GridModel& model, Probe& probe);
    ~GridCanvas() override;

    GridPanels& panels() noexcept { return *imgui; }
    bool    dragging() const noexcept { return drag != Drag::None; }
    int     hoverPitch() const noexcept { return hoverRow; }

protected:
    void onNanoDisplay() override;
    bool onMouse(const MouseEvent& ev) override;
    bool onMotion(const MotionEvent& ev) override;
    bool onScroll(const ScrollEvent& ev) override;
    bool onKeyboard(const KeyboardEvent& ev) override;
    void onResize(const ResizeEvent& ev) override;

private:
    friend struct GridSelfTest;

    float sf();
    // The grid's area (logical): below ImGui's menu bar, left of its panel.
    float gridX() { return kKeysW; }
    float gridY();
    float gridW();
    float gridH();
    bool  inGrid(float x, float y);
    int   pitchAt(float y);
    int   stepAt(float x);
    float rowY(int pitch);
    void  centreOn(int row);   // scrolls so `row` is in the middle
    void  changed();   // the model changed here: redraw, and the panels too

    static constexpr float kKeysW = 44, kRowH = 14, kStepW = 22;

    enum class Drag { None, Move };

    GridModel&              model;
    Probe&                  probe;
    std::unique_ptr<GridPanels> imgui;
    float                   scrollY  = 0;   // logical pixels from the top row
    int                     hoverRow = -1;
    // The tuning last drawn, and the pitch in the middle of the view then.
    int                     tunedEdo = 12, tunedReference = 60;
    double                  centrePitch = 60;
    Drag                    drag     = Drag::None;
    int                     grabStep = 0, grabPitch = 0;   // where in the note the drag took it
};

} // namespace umpire::ui
