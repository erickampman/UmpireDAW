#include "ImGuiLayer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Application.hpp"
#include "OpenGL.hpp"
#include "DearImGui/imgui_internal.h"
#include "DearImGui/imgui_impl_opengl2.h"

#include "NoteCanvas.h"

namespace umpire::spike {

namespace dgl = DGL_NAMESPACE;

ImGuiLayer::ImGuiLayer(NoteCanvas* parent, Model& m, Probe& p)
    : ImGuiSubWidget(parent),
      // ImGuiWidget's PrivateData (which holds the context) is opaque outside
      // DearImGui.cpp, but its constructor leaves the new context current.
      context(ImGui::GetCurrentContext()),
      model(m),
      probe(p) {
    ImGuiIO& io = ImGui::GetIO();
    // Keyboard navigation makes ImGui claim every key whenever one of its
    // windows has focus (io.WantCaptureKeyboard), which would take the canvas's
    // shortcuts away after any click on the panel.
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    // A blinking caret needs a frame every half second for as long as a field is active.
    io.ConfigInputTextCursorBlink = false;
}

void ImGuiLayer::wake(int frames) noexcept {
    framesOwed = std::max(framesOwed, frames);
}

void ImGuiLayer::rescale(double) {
    pendingRescale = true;   // done in onDisplay(), where the GL context is current
    wake();
}

bool ImGuiLayer::textActive() const {
    const ImGuiContext& g = *context;
    return g.ActiveId != 0 && g.InputTextState.ID == g.ActiveId;
}

bool ImGuiLayer::popupOpen() const {
    return context->OpenPopupStack.Size > 0;
}

bool ImGuiLayer::overWindow(double x, double y) const {
    ImGui::SetCurrentContext(context);
    ImGuiWindow* hovered = nullptr;
    ImGuiWindow* underMoving = nullptr;
    ImGui::FindHoveredWindowEx(ImVec2(static_cast<float>(x), static_cast<float>(y)), false, &hovered, &underMoving);
    return hovered != nullptr;
}

// --- drawing --------------------------------------------------------------------------

void ImGuiLayer::onImGuiDisplay() {
    const float W  = static_cast<float>(getWidth());
    const float H  = static_cast<float>(getHeight());
    const float sf = static_cast<float>(getWindow().getScaleFactor());

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New clip")) {
                model.notes.clear();
                model.selected = -1;
                model.clipName = "Untitled clip";
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Ctrl+Q")) quitRequested = true;
            ImGui::EndMenu();
        }
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float  w = ImGui::CalcTextSize("Edit").x + ImGui::GetStyle().ItemSpacing.x * 2;
            editMenuRect   = {p.x, 0, p.x + w, ImGui::GetFrameHeight()};
        }
        editMenuOpen = ImGui::BeginMenu("Edit");
        if (editMenuOpen) {
            if (ImGui::MenuItem("Delete note", "Del", false, model.selected >= 0)) model.deleteSelected();
            if (ImGui::MenuItem("Clear all")) {
                model.notes.clear();
                model.selected = -1;
            }
            clearAllItemRect = {ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y, ImGui::GetItemRectMax().x,
                                ImGui::GetItemRectMax().y};
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Redraw every frame (DPF-Widgets' default)", nullptr, &everyFrame);
            ImGui::MenuItem("Dear ImGui demo window", nullptr, &showDemo);
            ImGui::EndMenu();
        }
        menuH = ImGui::GetWindowSize().y;
        ImGui::EndMainMenuBar();
    }

    panelW = std::round(280.0f * sf);
    ImGui::SetNextWindowPos(ImVec2(W - panelW, menuH));
    ImGui::SetNextWindowSize(ImVec2(panelW, H - menuH));
    ImGui::Begin("Inspector", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);

    ImGui::TextUnformatted("Clip name");
    if (!textActive()) std::snprintf(nameBuf, sizeof nameBuf, "%s", model.clipName.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("##name", nameBuf, sizeof nameBuf)) model.clipName = nameBuf;
    nameFieldRect = {ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y, ImGui::GetItemRectMax().x,
                     ImGui::GetItemRectMax().y};

    ImGui::Separator();
    if (model.selected >= 0 && model.selected < static_cast<int>(model.notes.size())) {
        Note& n = model.notes[static_cast<std::size_t>(model.selected)];
        ImGui::TextUnformatted("Selected note");
        ImGui::DragInt("Pitch", &n.pitch, 0.2f, 0, 127);
        ImGui::SliderInt("Velocity", &n.velocity, 1, 127);
        velocityRect = {ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y, ImGui::GetItemRectMax().x,
                        ImGui::GetItemRectMax().y};
        ImGui::DragInt("Start", &n.start, 0.2f, 0, model.steps - 1);
        ImGui::DragInt("Length", &n.length, 0.2f, 1, model.steps);
        if (ImGui::Button("Delete note")) model.deleteSelected();
    } else {
        ImGui::TextDisabled("No note selected.");
        ImGui::TextDisabled("Click the grid to add one.");
        velocityRect = {};
    }

    ImGui::Separator();
    ImGui::Text("Redraw: %s", everyFrame ? "every frame" : "on demand");
    ImGui::Text("Frames drawn: %llu", static_cast<unsigned long long>(probe.frames));
    ImGui::Text("Frames, last second: %d", probe.framesLastSecond);
    ImGui::Text("Scale factor: %.2f", static_cast<double>(sf));
    ImGui::Text("Presses: canvas %d, ImGui %d", probe.canvasPresses, probe.imguiPresses);
    ImGui::Text("Keys: canvas %d, ImGui %d", probe.canvasKeys, probe.imguiKeys);
    ImGui::Text("Scrolls: canvas %d, ImGui %d", probe.canvasScrolls, probe.imguiScrolls);
    ImGui::Text("ImGui %s", IMGUI_VERSION);
    ImGui::End();

    if (showDemo) ImGui::ShowDemoWindow(&showDemo);
}

void ImGuiLayer::onDisplay() {
    if (pendingRescale) {
        pendingRescale = false;
        ImGui::SetCurrentContext(context);
        const double sf    = getWindow().getScaleFactor();
        ImGuiStyle&  style = ImGui::GetStyle();
        style              = ImGuiStyle();
        ImGui::StyleColorsDark();
        style.ScaleAllSizes(static_cast<float>(sf));
        setFontSize(fontSize);
        // DPF-Widgets' setFontSize() rebuilds the atlas but not the GL texture
        // made from it, so the old glyphs would be drawn at the new places.
        if (!rescaleLikeStock) {
            ImGui_ImplOpenGL2_DestroyFontsTexture();
            ImGui_ImplOpenGL2_CreateFontsTexture();
        }
    }

    ImGuiSubWidget::onDisplay();

    ++probe.frames;
    ++probe.framesThisSecond;
    const double now = getApp().getTime();
    if (now - probe.secondStart >= 1.0) {
        probe.framesLastSecond = probe.framesThisSecond;
        probe.framesThisSecond = 0;
        probe.secondStart      = now;
        if (probe.logFrames) std::printf("frames in the last second: %d\n", probe.framesLastSecond), std::fflush(stdout);
    }
    if (framesOwed > 0) --framesOwed;
    if (capturePath != nullptr) writeCapture();
}

void ImGuiLayer::idleCallback() {
    // ImGui trickles queued input over several frames (one press and release of
    // a key per frame), so fast typing needs frames until its queue is empty.
    if (everyFrame || framesOwed > 0 || context->InputEventsQueue.Size > 0) repaint();
}

// The window's back buffer, now that everything is drawn, as a binary PPM.
void ImGuiLayer::writeCapture() {
    const int w = static_cast<int>(getWindow().getWidth());
    const int h = static_cast<int>(getWindow().getHeight());
    std::vector<unsigned char> pixels(static_cast<std::size_t>(w * h * 3));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    if (std::FILE* f = std::fopen(capturePath, "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; --y) std::fwrite(&pixels[static_cast<std::size_t>(y * w * 3)], 1, static_cast<std::size_t>(w * 3), f);
        std::fclose(f);
    }
    capturePath = nullptr;
}

// --- input ----------------------------------------------------------------------------

bool ImGuiLayer::onMouse(const MouseEvent& ev) {
    const std::uint32_t bit = 1u << ev.button;
    bool toImGui;
    if (ev.press) {
        // Where the press lands, by last frame's window rectangles: io.WantCaptureMouse
        // is only updated by a frame, and a move then a click can arrive in one batch.
        toImGui = popupOpen() || overWindow(ev.pos.getX(), ev.pos.getY());
        (toImGui ? imguiButtons : canvasButtons) |= bit;
        ++(toImGui ? probe.imguiPresses : probe.canvasPresses);
    } else {
        // A release goes to whoever took the press, wherever it happens.
        toImGui = (imguiButtons & bit) != 0;
        imguiButtons &= ~bit;
        canvasButtons &= ~bit;
    }
    // ImGui hears every press, even the canvas's: a click outside its windows
    // is how it knows to stop editing text.
    ImGui::SetCurrentContext(context);
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(ev.pos.getX()), static_cast<float>(ev.pos.getY()));
    ImGuiSubWidget::onMouse(ev);
    wake();
    return toImGui;
}

bool ImGuiLayer::onMotion(const MotionEvent& ev) {
    ImGuiSubWidget::onMotion(ev);   // feeds the position; always returns false
    const bool over = overWindow(ev.pos.getX(), ev.pos.getY());
    // Frames for ImGui's hover highlights, while over its windows or just leaving them.
    if (over || lastOverWindow || imguiButtons != 0) wake();
    lastOverWindow = over;
    if (canvasButtons != 0) return false;   // the canvas's drag, even over the panel
    if (imguiButtons != 0) return true;     // ImGui's drag, even over the canvas
    return over;                            // hovering ImGui: no canvas hover under it
}

bool ImGuiLayer::onScroll(const ScrollEvent& ev) {
    const bool toImGui = popupOpen() || overWindow(ev.pos.getX(), ev.pos.getY());
    if (!toImGui) {
        ++probe.canvasScrolls;
        return false;
    }
    ++probe.imguiScrolls;
    ImGuiSubWidget::onScroll(ev);
    wake();
    return true;
}

bool ImGuiLayer::onKeyboard(const KeyboardEvent& ev) {
    // ImGui hears every key so it never misses a release, but takes them only
    // while it's editing text or has a menu open.
    const bool toImGui = textActive() || popupOpen();
    ImGuiSubWidget::onKeyboard(ev);
    if (!toImGui) return false;
    if (ev.press) ++probe.imguiKeys;
    wake();
    return true;
}

bool ImGuiLayer::onCharacterInput(const CharacterInputEvent& ev) {
    if (!textActive()) return false;
    ImGuiSubWidget::onCharacterInput(ev);
    wake();
    return true;
}

} // namespace umpire::spike
