#pragma once

// The NanoVG canvas: a note grid (rows are pitches, columns sixteenths) filling
// the window's top-level widget, as StageHand's windows do (UiWidget). The
// ImGui layer is its subwidget, so it draws over the canvas and sees input
// first; the canvas handles what ImGui passes on.
//
// Coordinates are logical pixels; the window's scale factor is applied when
// drawing (StageHand's convention). ImGui works in window pixels.

#include <memory>

#include "NanoVG.hpp"
#include "Window.hpp"

#include "SpikeModel.h"

namespace umpire::spike {

class ImGuiLayer;

class NoteCanvas final : public DGL_NAMESPACE::NanoTopLevelWidget {
public:
    NoteCanvas(DGL_NAMESPACE::Window& window, Model& model, Probe& probe);
    ~NoteCanvas() override;

    ImGuiLayer& layer() noexcept { return *imgui; }
    bool dragging() const noexcept { return drag != Drag::None; }
    int  hoverPitch() const noexcept { return hoverRow; }

protected:
    void onNanoDisplay() override;
    bool onMouse(const MouseEvent& ev) override;
    bool onMotion(const MotionEvent& ev) override;
    bool onScroll(const ScrollEvent& ev) override;
    bool onKeyboard(const KeyboardEvent& ev) override;
    void onResize(const ResizeEvent& ev) override;

private:
    friend struct SelfTest;

    float sf();
    // The grid's area (logical): below ImGui's menu bar, left of its panel.
    float gridX() { return kKeysW; }
    float gridY();
    float gridW();
    float gridH();
    int   pitchAt(float y);
    int   stepAt(float x);
    float rowY(int pitch);
    int   noteAt(float x, float y);   // index, or -1

    static constexpr float kKeysW = 44, kRowH = 14, kStepW = 22;

    enum class Drag { None, Move };

    Model&                      model;
    Probe&                      probe;
    std::unique_ptr<ImGuiLayer> imgui;
    float                       scrollY  = 0;   // logical pixels from pitch 127's row
    int                         hoverRow = -1;
    Drag                        drag     = Drag::None;
    int                         grabStep = 0, grabPitch = 0;   // where in the note the drag took it
};

} // namespace umpire::spike
