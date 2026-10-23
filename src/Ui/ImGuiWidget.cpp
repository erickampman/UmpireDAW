/*
 * Umpire's ImGui widget, adapted from DPF-Widgets' opengl/DearImGui.cpp:
 *
 * Dear ImGui for DPF
 * Copyright (C) 2021 Jean Pierre Cimalando <jp-dev@inbox.ru>
 * Copyright (C) 2021-2025 Filipe Coelho <falktx@falktx.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with
 * or without fee is hereby granted, provided that the above copyright notice and this
 * permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "Ui/ImGuiWidget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Application.hpp"
#include "OpenGL.hpp"
#include "TopLevelWidget.hpp"
#include "Window.hpp"
#include "src/Resources.hpp"   // DGL's shared resources: DejaVu Sans

#include "imgui.h"
#include "imgui_impl_opengl2.h"
#include "imgui_internal.h"

namespace umpire::ui {

namespace dgl = DGL_NAMESPACE;

namespace {

const char* clipboardText(void* user) {
    std::size_t size = 0;
    return static_cast<const char*>(static_cast<dgl::TopLevelWidget*>(user)->getClipboard(size));
}

void setClipboardText(void* user, const char* text) {
    if (text != nullptr) static_cast<dgl::TopLevelWidget*>(user)->setClipboard(nullptr, text, std::strlen(text) + 1);
}

// DGL's key (a character, or one of its kKey* codes) as ImGui's.
ImGuiKey imguiKey(std::uint32_t key) {
    if (key >= 'a' && key <= 'z') return static_cast<ImGuiKey>(ImGuiKey_A + (key - 'a'));
    if (key >= '0' && key <= '9') return static_cast<ImGuiKey>(ImGuiKey_0 + (key - '0'));
    if (key >= dgl::kKeyF1 && key <= dgl::kKeyF12) return static_cast<ImGuiKey>(ImGuiKey_F1 + (key - dgl::kKeyF1));
    if (key >= dgl::kKeyPad0 && key <= dgl::kKeyPad9)
        return static_cast<ImGuiKey>(ImGuiKey_Keypad0 + (key - dgl::kKeyPad0));
    switch (key) {
    case '\t': return ImGuiKey_Tab;
    case '\'': return ImGuiKey_Apostrophe;
    case ',': return ImGuiKey_Comma;
    case '-': return ImGuiKey_Minus;
    case '.': return ImGuiKey_Period;
    case '/': return ImGuiKey_Slash;
    case ';': return ImGuiKey_Semicolon;
    case '=': return ImGuiKey_Equal;
    case '[': return ImGuiKey_LeftBracket;
    case '\\': return ImGuiKey_Backslash;
    case ']': return ImGuiKey_RightBracket;
    case '`': return ImGuiKey_GraveAccent;
    case dgl::kKeyBackspace: return ImGuiKey_Backspace;
    case dgl::kKeyEnter: return ImGuiKey_Enter;
    case dgl::kKeyEscape: return ImGuiKey_Escape;
    case dgl::kKeyDelete: return ImGuiKey_Delete;
    case dgl::kKeySpace: return ImGuiKey_Space;
    case dgl::kKeyPageUp: return ImGuiKey_PageUp;
    case dgl::kKeyPageDown: return ImGuiKey_PageDown;
    case dgl::kKeyEnd: return ImGuiKey_End;
    case dgl::kKeyHome: return ImGuiKey_Home;
    case dgl::kKeyLeft: return ImGuiKey_LeftArrow;
    case dgl::kKeyUp: return ImGuiKey_UpArrow;
    case dgl::kKeyRight: return ImGuiKey_RightArrow;
    case dgl::kKeyDown: return ImGuiKey_DownArrow;
    case dgl::kKeyPrintScreen: return ImGuiKey_PrintScreen;
    case dgl::kKeyInsert: return ImGuiKey_Insert;
    case dgl::kKeyPause: return ImGuiKey_Pause;
    case dgl::kKeyMenu: return ImGuiKey_Menu;
    case dgl::kKeyNumLock: return ImGuiKey_NumLock;
    case dgl::kKeyScrollLock: return ImGuiKey_ScrollLock;
    case dgl::kKeyCapsLock: return ImGuiKey_CapsLock;
    case dgl::kKeyShiftL: return ImGuiKey_LeftShift;
    case dgl::kKeyShiftR: return ImGuiKey_RightShift;
    case dgl::kKeyControlL: return ImGuiKey_LeftCtrl;
    case dgl::kKeyControlR: return ImGuiKey_RightCtrl;
    case dgl::kKeyAltL: return ImGuiKey_LeftAlt;
    case dgl::kKeyAltR: return ImGuiKey_RightAlt;
    case dgl::kKeySuperL: return ImGuiKey_LeftSuper;
    case dgl::kKeySuperR: return ImGuiKey_RightSuper;
    case dgl::kKeyPadEqual: return ImGuiKey_KeypadEqual;
    case dgl::kKeyPadMultiply: return ImGuiKey_KeypadMultiply;
    case dgl::kKeyPadAdd: return ImGuiKey_KeypadAdd;
    case dgl::kKeyPadSubtract: return ImGuiKey_KeypadSubtract;
    case dgl::kKeyPadDecimal: return ImGuiKey_KeypadDecimal;
    case dgl::kKeyPadDivide: return ImGuiKey_KeypadDivide;
    default: return ImGuiKey_None;
    }
}

} // namespace

ImGuiWidget::ImGuiWidget(dgl::Widget* parent, float fontSize) : SubWidget(parent), fontLogical(fontSize) {
    IMGUI_CHECKVERSION();
    context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGuiIO& io     = ImGui::GetIO();
    io.IniFilename  = nullptr;
    io.LogFilename  = nullptr;
    io.DisplaySize  = ImVec2(static_cast<float>(std::max(1u, getWidth())), static_cast<float>(std::max(1u, getHeight())));
    // Keyboard navigation would make ImGui claim every key whenever one of its
    // windows has focus, taking the canvas's shortcuts after any click on a panel.
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    // A blinking caret needs a frame every half second while a field is active.
    io.ConfigInputTextCursorBlink = false;
    io.GetClipboardTextFn = clipboardText;
    io.SetClipboardTextFn = setClipboardText;
    io.ClipboardUserData  = getTopLevelWidget();
    ImGui_ImplOpenGL2_Init();
    applyScale();
    getWindow().addIdleCallback(this, 1000 / 60);
}

ImGuiWidget::~ImGuiWidget() {
    getWindow().removeIdleCallback(this);
    ImGui::SetCurrentContext(context);
    ImGui_ImplOpenGL2_Shutdown();
    ImGui::DestroyContext(context);
}

void ImGuiWidget::wake(int framesWanted) noexcept {
    framesOwed = std::max(framesOwed, framesWanted);
}

void ImGuiWidget::rescale() noexcept {
    pendingRescale = true;   // done in onDisplay(), where the GL context is current
    wake();
}

void ImGuiWidget::setFontSize(float logical) noexcept {
    fontLogical = logical;
    rescale();
}

bool ImGuiWidget::textActive() const {
    const ImGuiContext& g = *context;
    return g.ActiveId != 0 && g.InputTextState.ID == g.ActiveId;
}

bool ImGuiWidget::popupOpen() const {
    return context->OpenPopupStack.Size > 0;
}

bool ImGuiWidget::overWindow(double x, double y) const {
    ImGui::SetCurrentContext(context);
    ImGuiWindow* hovered     = nullptr;
    ImGuiWindow* underMoving = nullptr;
    ImGui::FindHoveredWindowEx(ImVec2(static_cast<float>(x), static_cast<float>(y)), false, &hovered, &underMoving);
    return hovered != nullptr;
}

void ImGuiWidget::onStyle(ImGuiStyle&) {
    ImGui::StyleColorsDark();
}

void ImGuiWidget::applyScale() {
    ImGui::SetCurrentContext(context);
    const float sf    = static_cast<float>(getWindow().getScaleFactor());
    ImGuiStyle& style = ImGui::GetStyle();
    style             = ImGuiStyle();
    onStyle(style);
    style.ScaleAllSizes(sf);

    using namespace dpf_resources;
    ImGuiIO&     io = ImGui::GetIO();
    ImFontConfig fc;
    fc.FontDataOwnedByAtlas = false;
    fc.OversampleH          = 1;
    fc.OversampleV          = 1;
    fc.PixelSnapH           = true;
    io.Fonts->Clear();
    io.Fonts->AddFontFromMemoryTTF(const_cast<char*>(dejavusans_ttf), static_cast<int>(dejavusans_ttf_size),
                                   std::max(1.0f, std::round(fontLogical * sf)), &fc);
    io.Fonts->Build();
    // The GL texture made from the atlas: DPF-Widgets' setFontSize() forgot it,
    // so the old glyphs were drawn at the new places (finding 7).
    ImGui_ImplOpenGL2_DestroyFontsTexture();
    ImGui_ImplOpenGL2_CreateFontsTexture();
}

void ImGuiWidget::modifiers(unsigned mods) {
    if (mods == lastModifiers) return;
    ImGuiIO&       io      = ImGui::GetIO();
    const unsigned changed = mods ^ lastModifiers;
    if (changed & dgl::kModifierShift) io.AddKeyEvent(ImGuiMod_Shift, (mods & dgl::kModifierShift) != 0);
    if (changed & dgl::kModifierControl) io.AddKeyEvent(ImGuiMod_Ctrl, (mods & dgl::kModifierControl) != 0);
    if (changed & dgl::kModifierAlt) io.AddKeyEvent(ImGuiMod_Alt, (mods & dgl::kModifierAlt) != 0);
    if (changed & dgl::kModifierSuper) io.AddKeyEvent(ImGuiMod_Super, (mods & dgl::kModifierSuper) != 0);
    lastModifiers = mods;
}

// --- drawing --------------------------------------------------------------------------

void ImGuiWidget::onDisplay() {
    ImGui::SetCurrentContext(context);
    if (pendingRescale) {
        pendingRescale = false;
        applyScale();
    }
    ImGuiIO&     io  = ImGui::GetIO();
    const double now = getApp().getTime();
    io.DeltaTime     = lastFrameTime > 0 && now > lastFrameTime ? static_cast<float>(now - lastFrameTime) : 1.0f / 60;
    lastFrameTime    = now;

    ImGui_ImplOpenGL2_NewFrame();
    ImGui::NewFrame();
    onImGuiDisplay();
    ImGui::Render();
    if (ImDrawData* data = ImGui::GetDrawData()) {
        // The backend's projection, for a subwidget anywhere in the window (DPF-Widgets').
        data->DisplayPos.x = -static_cast<float>(getAbsoluteX());
        data->DisplayPos.y = static_cast<float>(getWindow().getHeight()) - static_cast<float>(getAbsoluteY()) -
                             static_cast<float>(getHeight());
        ImGui_ImplOpenGL2_RenderDrawData(data);
    }

    ++frames;
    if (framesOwed > 0) --framesOwed;
    if (!capturePath.empty()) writeCapture();
}

void ImGuiWidget::idleCallback() {
    // ImGui trickles queued input over several frames (one press and release of
    // a key per frame), so fast typing needs frames until its queue is empty.
    if (framesOwed > 0 || context->InputEventsQueue.Size > 0) repaint();
}

// The window's back buffer, now that everything is drawn, as a binary PPM.
void ImGuiWidget::writeCapture() {
    const int w = static_cast<int>(getWindow().getWidth());
    const int h = static_cast<int>(getWindow().getHeight());
    std::vector<unsigned char> pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    if (std::FILE* f = std::fopen(capturePath.c_str(), "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = h - 1; y >= 0; --y)
            std::fwrite(&pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 3], 1,
                        static_cast<std::size_t>(w) * 3, f);
        std::fclose(f);
    }
    capturePath.clear();
}

// --- input ----------------------------------------------------------------------------
// The parent offers each event to this widget first (TopLevelWidget's default);
// what's returned decides whether the parent sees it too.

bool ImGuiWidget::onMouse(const MouseEvent& ev) {
    const std::uint32_t bit = 1u << ev.button;
    bool                toImGui;
    if (ev.press) {
        // Where the press lands, by last frame's windows: io.WantCaptureMouse is
        // only updated by a frame, and a move then a click can come in one batch.
        toImGui = popupOpen() || overWindow(ev.pos.getX(), ev.pos.getY());
        (toImGui ? imguiButtons : parentButtons) |= bit;
        routed(Input::Press, toImGui);
    } else {
        toImGui = (imguiButtons & bit) != 0;   // whoever took the press, wherever it's let go
        imguiButtons &= ~bit;
        parentButtons &= ~bit;
    }
    // ImGui hears every press, even the parent's: a click outside its windows
    // is how it knows to stop editing text, or to close a menu.
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    modifiers(ev.mod);
    io.AddMousePosEvent(static_cast<float>(ev.pos.getX()), static_cast<float>(ev.pos.getY()));
    switch (ev.button) {
    case dgl::kMouseButtonLeft: io.AddMouseButtonEvent(ImGuiMouseButton_Left, ev.press); break;
    case dgl::kMouseButtonRight: io.AddMouseButtonEvent(ImGuiMouseButton_Right, ev.press); break;
    case dgl::kMouseButtonMiddle: io.AddMouseButtonEvent(ImGuiMouseButton_Middle, ev.press); break;
    default: break;
    }
    wake();
    return toImGui;
}

bool ImGuiWidget::onMotion(const MotionEvent& ev) {
    ImGui::SetCurrentContext(context);
    modifiers(ev.mod);
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(ev.pos.getX()), static_cast<float>(ev.pos.getY()));
    const bool over = overWindow(ev.pos.getX(), ev.pos.getY());
    // Frames for ImGui's hover highlights, while over its windows or just leaving them.
    if (over || lastOverWindow || imguiButtons != 0) wake();
    lastOverWindow = over;
    if (parentButtons != 0) return false;   // the parent's drag, even over a panel
    if (imguiButtons != 0) return true;     // ImGui's drag, even over the parent
    return over;                            // hovering ImGui: no hover under it
}

bool ImGuiWidget::onScroll(const ScrollEvent& ev) {
    const bool toImGui = popupOpen() || overWindow(ev.pos.getX(), ev.pos.getY());
    routed(Input::Scroll, toImGui);
    if (!toImGui) return false;
    ImGui::SetCurrentContext(context);
    modifiers(ev.mod);
    ImGui::GetIO().AddMouseWheelEvent(static_cast<float>(ev.delta.getX()), static_cast<float>(ev.delta.getY()));
    wake();
    return true;
}

bool ImGuiWidget::onKeyboard(const KeyboardEvent& ev) {
    const bool toImGui = textActive() || popupOpen();
    ImGui::SetCurrentContext(context);
    modifiers(ev.mod);
    if (const ImGuiKey key = imguiKey(ev.key); key != ImGuiKey_None) ImGui::GetIO().AddKeyEvent(key, ev.press);
    // Escape closes menus and dropdowns. ImGui does that through keyboard
    // navigation, which is off here.
    if (ev.press && ev.key == dgl::kKeyEscape && popupOpen() && !textActive()) ImGui::ClosePopupToLevel(0, true);
    if (ev.press) routed(Input::Key, toImGui);
    if (!toImGui) return false;
    wake();
    return true;
}

bool ImGuiWidget::onCharacterInput(const CharacterInputEvent& ev) {
    if (!textActive()) return false;
    ImGui::SetCurrentContext(context);
    modifiers(ev.mod);
    ImGui::GetIO().AddInputCharacter(ev.character);
    wake();
    return true;
}

void ImGuiWidget::onResize(const ResizeEvent& ev) {
    SubWidget::onResize(ev);
    ImGui::SetCurrentContext(context);
    ImGui::GetIO().DisplaySize =
        ImVec2(static_cast<float>(ev.size.getWidth()), static_cast<float>(ev.size.getHeight()));
    wake();
}

} // namespace umpire::ui
