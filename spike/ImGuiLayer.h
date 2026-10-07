#pragma once

// The ImGui layer: DPF-Widgets' ImGuiSubWidget covering the whole window, over
// the NanoVG canvas, with a menu bar and an inspector panel. It is the canvas's
// last subwidget, so DGL draws it last and gives it every input event first;
// what it returns decides whether the canvas sees the event too.
//
// Two changes from the stock widget, both made by overriding (DPF-Widgets is
// unpatched):
// - Routing: a mouse press goes to ImGui only if it lands on an ImGui window
//   (tested at the press position, not from the last frame's hover state) or a
//   popup is open; the side that took the press keeps the drag until release.
//   Keys go to ImGui only while it's editing text or has a popup open.
// - Redraw: the stock widget repaints at 60 fps forever. Here, input schedules
//   a few frames and idle repaints only while frames are owed.

#include <cstdint>

#include "DearImGui.hpp"
#include "SpikeModel.h"

namespace umpire::spike {

class NoteCanvas;

class ImGuiLayer final : public DGL_NAMESPACE::ImGuiSubWidget {
public:
    ImGuiLayer(NoteCanvas* canvas, Model& model, Probe& probe);

    // The area the canvas may draw in (window pixels): below the menu bar and
    // left of the inspector. Known after the first frame.
    float menuBarHeight() const noexcept { return menuH; }
    float panelWidth() const noexcept { return panelW; }

    // Redraw: false (Umpire's) draws on demand; true is DPF-Widgets' stock 60 fps.
    bool everyFrame = false;
    bool quitRequested = false;
    bool showDemo = false;

    // Ask for a few frames (after input, or a model change from outside).
    void wake(int frames = 3) noexcept;
    // The scale factor changed (another monitor): rebuild the font and style.
    void rescale(double scaleFactor);
    // The next frame is written to this file (binary PPM) once drawn.
    void captureNextFrame(const char* path) noexcept { capturePath = path; }
    // Rescale as DPF-Widgets' setFontSize() alone would (to show what it gets wrong).
    bool rescaleLikeStock = false;
    float fontSize = 13.0f;   // logical; rescale() applies it

    // For the self-test: where things were drawn in the last frame (window pixels).
    struct Rect { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; float cx() const { return (x0 + x1) / 2; } float cy() const { return (y0 + y1) / 2; } };
    Rect editMenuRect, clearAllItemRect, nameFieldRect, velocityRect;
    bool editMenuOpen = false;
    bool textActive() const;   // a text field is being edited
    bool popupOpen() const;

protected:
    void onImGuiDisplay() override;
    void onDisplay() override;
    void idleCallback() override;
    bool onMouse(const MouseEvent& ev) override;
    bool onMotion(const MotionEvent& ev) override;
    bool onScroll(const ScrollEvent& ev) override;
    bool onKeyboard(const KeyboardEvent& ev) override;
    bool onCharacterInput(const CharacterInputEvent& ev) override;

private:
    bool overWindow(double x, double y) const;   // an ImGui window is at this point (last frame's layout)
    void writeCapture();

    ImGuiContext* context;
    Model&        model;
    Probe&        probe;
    float         menuH  = 0;
    float         panelW = 0;
    int           framesOwed = 0;
    std::uint32_t imguiButtons  = 0;   // buttons whose press ImGui took (bit per DGL button)
    std::uint32_t canvasButtons = 0;   // buttons whose press the canvas took
    bool          lastOverWindow = false;
    bool          pendingRescale = false;
    const char*   capturePath = nullptr;
    char          nameBuf[128] = {};
};

} // namespace umpire::spike
