#pragma once

// Umpire's Dear ImGui widget for DGL: a SubWidget covering its parent, with
// its own ImGui context, drawing with ImGui's legacy-OpenGL backend over
// whatever the parent drew. Adapted from DPF-Widgets' ImGuiWidget
// (opengl/DearImGui.cpp, ISC; copyright in ImGuiWidget.cpp), which the spike
// subclassed (docs/ui-spike.md, proposal 2). What's different:
//
// - Routing (finding 4). A press goes to ImGui only if a popup is open or an
//   ImGui window is under it, tested at the press position against the last
//   frame's windows; the side that took a press keeps the motion and the
//   release until the button goes up. The wheel goes to what's under the
//   pointer. Keys go to ImGui only while it's editing text or a popup is open.
//   ImGui still hears every press and key, so it ends editing and never misses
//   a release. Keyboard navigation is off.
// - Redrawing (finding 5): on demand. Input that reaches ImGui, and wake(),
//   owe a few frames; the idle callback repaints only while frames are owed
//   or ImGui's input queue isn't empty. The caret doesn't blink.
// - Rescaling (finding 7): rescale() rebuilds the style, the font and the
//   font's GL texture, in the next frame, where the GL context is current.
//
// The font is DejaVu Sans, from DGL's shared resources.

#include <cstdint>
#include <string>

#include "SubWidget.hpp"

struct ImGuiContext;
struct ImGuiStyle;

namespace umpire::ui {

class ImGuiWidget : public DGL_NAMESPACE::SubWidget, public DGL_NAMESPACE::IdleCallback {
public:
    // `fontSize` in logical pixels. The GL context must be current.
    explicit ImGuiWidget(DGL_NAMESPACE::Widget* parent, float fontSize = 13.0f);
    ~ImGuiWidget() override;   // the GL context must be current

    // Ask for a few frames (after a change ImGui didn't see).
    void wake(int frames = 3) noexcept;
    // The scale factor changed (another monitor), or the font size: rebuild them.
    void  rescale() noexcept;
    void  setFontSize(float logical) noexcept;
    float fontSize() const noexcept { return fontLogical; }

    bool textActive() const;   // a text field is being edited
    bool popupOpen() const;
    bool overWindow(double x, double y) const;   // an ImGui window is here (window pixels, last frame)

    std::uint64_t framesDrawn() const noexcept { return frames; }
    // The next frame is written to this file (binary PPM) once drawn.
    void captureNextFrame(std::string path) { capturePath = std::move(path); }

protected:
    // Draw the frame with ImGui's calls; its context is current.
    virtual void onImGuiDisplay() = 0;
    // Set the style's colours and sizes; called before scaling it, at the start
    // and on every rescale. The default is ImGui's dark style.
    virtual void onStyle(ImGuiStyle& style);
    // Where an input went (for counting); `toImGui` is whether ImGui took it.
    enum class Input { Press, Key, Scroll };
    virtual void routed(Input, bool /*toImGui*/) {}

    void onDisplay() override;
    void idleCallback() override;
    bool onMouse(const MouseEvent& ev) override;
    bool onMotion(const MotionEvent& ev) override;
    bool onScroll(const ScrollEvent& ev) override;
    bool onKeyboard(const KeyboardEvent& ev) override;
    bool onCharacterInput(const CharacterInputEvent& ev) override;
    void onResize(const ResizeEvent& ev) override;

private:
    void applyScale();   // style and font for the window's scale factor; GL context current
    void modifiers(unsigned mods);
    void writeCapture();

    ImGuiContext* context = nullptr;
    float         fontLogical;
    double        lastFrameTime = 0;
    unsigned      lastModifiers = 0;
    int           framesOwed    = 0;
    std::uint64_t frames        = 0;
    std::uint32_t imguiButtons  = 0;   // buttons whose press ImGui took (bit per DGL button)
    std::uint32_t parentButtons = 0;   // buttons whose press went past ImGui
    bool          lastOverWindow = false;
    bool          pendingRescale = false;
    std::string   capturePath;
};

} // namespace umpire::ui
