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
#include <vector>

#include "NanoVG.hpp"
#include "Window.hpp"

#include "App/GridModel.h"
#include "Ui/Probe.h"
#include "Ui/ScrollAxis.h"

namespace umpire::ui {

class GridPanels;

// What a window shows above the grid, between the menu bar and the grid: the
// plugin's lane (milestone P3, docs/plugin-version.md). The app's grid
// windows have none. Coordinates are logical pixels from the band's top left;
// drawing has the window's scale applied.
class GridBand {
public:
    virtual ~GridBand() = default;
    // Its height, given the height below the menu bar.
    virtual float height(float available) = 0;
    virtual void  draw(DGL_NAMESPACE::NanoVG& vg, float width, float height) = 0;
    // A press in the band, and the motion and release that follow it; the
    // wheel over it. True if taken.
    virtual bool mouse(const DGL_NAMESPACE::Widget::MouseEvent&, float /*x*/, float /*y*/) { return false; }
    virtual bool motion(const DGL_NAMESPACE::Widget::MotionEvent&, float /*x*/, float /*y*/) { return false; }
    virtual bool scroll(const DGL_NAMESPACE::Widget::ScrollEvent&, float /*x*/, float /*y*/) { return false; }
    // Whether the grid shows (false: no clip; the band has the window).
    virtual bool gridShown() { return true; }
    // The playhead on the grid, in its ticks (-1: none), and whether it's
    // moving: while it is, the grid pages to keep it in view, until the
    // grid is scrolled by hand.
    virtual Tick playhead() { return -1; }
    virtual bool playing() { return false; }
};

class GridCanvas final : public DGL_NAMESPACE::NanoTopLevelWidget {
public:
    GridCanvas(DGL_NAMESPACE::Window& window, GridModel& model, Probe& probe);
    ~GridCanvas() override;

    GridPanels& panels() noexcept { return *imgui; }
    // The band above the grid (not owned; null: none, as in the app).
    void        setBand(GridBand* b) noexcept { band = b; }
    // Another clip shown: at the next draw, once the grid has its size, the
    // clip's first note (of a chord, its top) goes in the middle of the rows
    // and a quarter of the way across (or the grid's start, if that's
    // nearer). No notes: the start, the rows as they were.
    void        showFirstNote() noexcept { firstNotePending = true; }
    // The horizontal zoom (Ui/Zoom.h): a factor on kStepW. Command-wheel or
    // a pinch over the grid changes it, about the pointer. The canvas keeps
    // its own unless linked to one that outlives it (the plugin's, which its
    // state saves); null goes back to its own.
    float       zoom() const noexcept { return *zoomFactor; }
    void        setZoom(float factor);
    void        linkZoom(float* factor) noexcept { zoomFactor = factor != nullptr ? factor : &ownZoom; }
    bool    dragging() const noexcept { return drag != Drag::None && drag != Drag::Click; }
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
    float bandY();   // the band's top: below the menu bar
    float bandH();
    float gridY();
    float gridW();
    float gridH();
    bool  inGrid(float x, float y);
    int   pitchAt(float y);
    int   stepAt(float x);
    Tick  tickAt(float x);
    float tickX(Tick t);   // logical, from the grid's left
    float stepW() const noexcept { return kStepW * *zoomFactor; }   // a sixteenth, zoomed
    // The note whose right edge is under (x, y): a press there resizes.
    int   edgeAt(float x, float y);
    void  setPointer(DGL_NAMESPACE::MouseCursor cursor);
    float rowY(int pitch);
    void  centreOn(int row);   // scrolls so `row` is in the middle
    void  placeFirstNote(float gw);   // what showFirstNote() asked for, `gw` the grid's width
    void  clampScrollX();
    void  changed();   // the model changed here: redraw, and the panels too

    static constexpr float kKeysW = 44, kRowH = 14, kStepW = 22;   // kStepW: a sixteenth at zoom 1
    static constexpr float kEdge = 5;    // how near a note's right end grabs it
    static constexpr float kSlop = 4;    // how far a press goes before it's a drag

    // Click: a press on empty grid, a note added when let go, or a box once
    // it moves.
    enum class Drag { None, Click, Move, Resize, Box };

    GridModel&              model;
    Probe&                  probe;
    std::unique_ptr<GridPanels> imgui;
    float                   scrollY  = 0;   // logical pixels from the top row
    float                   scrollX  = 0;   // logical pixels from the first column
    int                     hoverRow = -1;
    // The tuning last drawn, and the pitch in the middle of the view then.
    int                     tunedEdo = 12, tunedReference = 60;
    double                  centrePitch = 60;
    Drag                    drag     = Drag::None;
    bool                    shiftAtPress = false;
    bool                    toggleOff    = false;   // Shift+click on a selected note: out, unless dragged
    float                   pressX = 0, pressY = 0;   // logical
    float                   boxX = 0, boxY = 0;       // the box's other corner
    int                     grabStep = 0, grabPitch = 0;   // where the drag took the grid
    Tick                    grabTick = 0;
    std::vector<bool>       boxBase;   // the selection when a Shift box began
    DGL_NAMESPACE::MouseCursor pointer = DGL_NAMESPACE::kMouseCursorArrow;
    GridBand*               band = nullptr;
    bool                    bandPressed  = false;   // the band took the press
    bool                    handScrolled = false;   // scrolled while playing: no paging until the next play
    bool                    wasPlaying   = false;
    ScrollAxis              axes;   // a two-finger drag keeps to one axis
    bool                    firstNotePending = true;   // a clip shown: place its first note at the next draw
    float                   ownZoom      = 1;
    float*                  zoomFactor   = &ownZoom;
};

} // namespace umpire::ui
