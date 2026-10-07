#include "NoteCanvas.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "ImGuiLayer.h"

namespace umpire::spike {

namespace dgl = DGL_NAMESPACE;

namespace {
bool blackKey(int pitch) {
    const int pc = ((pitch % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}
} // namespace

NoteCanvas::NoteCanvas(dgl::Window& window, Model& m, Probe& p)
    : NanoTopLevelWidget(window), model(m), probe(p) {
    loadSharedResources();   // DGL's built-in font (DejaVu Sans)
    imgui = std::make_unique<ImGuiLayer>(this, model, probe);
    imgui->setSize(getWidth(), getHeight());
    scrollY = rowY(72) - 40;   // C5 near the top
}

NoteCanvas::~NoteCanvas() = default;

float NoteCanvas::sf() {
    return static_cast<float>(getWindow().getScaleFactor());
}
float NoteCanvas::gridY() {
    return imgui->menuBarHeight() / sf() + 20;   // a strip for the clip name
}
float NoteCanvas::gridW() {
    return std::max(0.0f, (static_cast<float>(getWidth()) - imgui->panelWidth()) / sf() - gridX());
}
float NoteCanvas::gridH() {
    return std::max(0.0f, static_cast<float>(getHeight()) / sf() - gridY());
}
float NoteCanvas::rowY(int pitch) {
    return static_cast<float>(127 - pitch) * kRowH;
}
int NoteCanvas::pitchAt(float y) {
    return 127 - static_cast<int>(std::floor((y - gridY() + scrollY) / kRowH));
}
int NoteCanvas::stepAt(float x) {
    return static_cast<int>(std::floor((x - gridX()) / kStepW));
}
int NoteCanvas::noteAt(float x, float y) {
    const int pitch = pitchAt(y), step = stepAt(x);
    for (int i = static_cast<int>(model.notes.size()) - 1; i >= 0; --i) {
        const Note& n = model.notes[static_cast<std::size_t>(i)];
        if (n.pitch == pitch && step >= n.start && step < n.start + n.length) return i;
    }
    return -1;
}

// --- drawing --------------------------------------------------------------------------

void NoteCanvas::onNanoDisplay() {
    const float s = sf();
    scale(s, s);
    const float W = static_cast<float>(getWidth()) / s, H = static_cast<float>(getHeight()) / s;
    const float gx = gridX(), gy = gridY(), gw = gridW(), gh = gridH();

    beginPath();
    rect(0, 0, W, H);
    fillColor(22, 24, 30);
    fill();

    fontSize(13);
    textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
    fillColor(140, 146, 160);
    text(gx, gy - 10, model.clipName.c_str(), nullptr);

    save();
    scissor(0, gy, gx + gw, gh);
    for (int pitch = 127; pitch >= 0; --pitch) {
        const float y = gy + rowY(pitch) - scrollY;
        if (y + kRowH < gy || y > gy + gh) continue;
        beginPath();
        rect(gx, y, gw, kRowH);
        if (pitch == hoverRow) fillColor(52, 58, 74);
        else if (blackKey(pitch)) fillColor(28, 30, 38);
        else fillColor(35, 38, 47);
        fill();
        beginPath();   // the keyboard
        rect(0, y, gx - 1, kRowH - 1);
        if (blackKey(pitch)) fillColor(40, 42, 50);
        else fillColor(200, 202, 210);
        fill();
        if (pitch % 12 == 0) {
            fillColor(20, 20, 24);
            fontSize(10);
            text(3, y + kRowH / 2, ("C" + std::to_string(pitch / 12 - 1)).c_str(), nullptr);
        }
    }
    for (int step = 0; step <= model.steps; ++step) {
        const float x = gx + static_cast<float>(step) * kStepW;
        if (x > gx + gw) break;
        beginPath();
        moveTo(x, gy);
        lineTo(x, gy + gh);
        strokeWidth(step % 16 == 0 ? 1.5f : 1.0f);
        if (step % 16 == 0) strokeColor(110, 116, 130);
        else if (step % 4 == 0) strokeColor(64, 68, 80);
        else strokeColor(44, 48, 58);
        stroke();
    }
    for (std::size_t i = 0; i < model.notes.size(); ++i) {
        const Note& n = model.notes[i];
        const float x = gx + static_cast<float>(n.start) * kStepW, y = gy + rowY(n.pitch) - scrollY;
        const int   v = 90 + n.velocity;   // brighter when louder
        beginPath();
        roundedRect(x + 1, y + 1, static_cast<float>(n.length) * kStepW - 2, kRowH - 2, 3);
        fillColor(std::min(v, 255) / 3, std::min(v, 255) * 2 / 3, std::min(v, 255));
        fill();
        if (static_cast<int>(i) == model.selected) {
            strokeColor(240, 170, 50);
            strokeWidth(2);
            stroke();
        }
    }
    restore();
}

// --- input ----------------------------------------------------------------------------
// Each handler offers the event to the ImGui layer first (TopLevelWidget's
// default passes it to the subwidgets); the canvas handles what's left.

bool NoteCanvas::onMouse(const MouseEvent& ev) {
    if (NanoTopLevelWidget::onMouse(ev)) return true;
    const float x = static_cast<float>(ev.pos.getX()) / sf(), y = static_cast<float>(ev.pos.getY()) / sf();
    if (!ev.press) {
        if (ev.button == dgl::kMouseButtonLeft && drag != Drag::None) {
            drag = Drag::None;
            repaint();
            return true;
        }
        return false;
    }
    if (x < gridX() || x >= gridX() + gridW() || y < gridY() || y >= gridY() + gridH()) return false;
    int hit = noteAt(x, y);
    if (ev.button == dgl::kMouseButtonRight) {
        if (hit >= 0) {
            model.selected = hit;
            model.deleteSelected();
        }
    } else if (ev.button == dgl::kMouseButtonLeft) {
        if (hit < 0) {
            const int step = stepAt(x);
            if (step >= model.steps) return true;
            model.notes.push_back(Note{pitchAt(y), step, 2, 100});
            hit = static_cast<int>(model.notes.size()) - 1;
        }
        model.selected = hit;
        const Note& n  = model.notes[static_cast<std::size_t>(hit)];
        grabStep       = stepAt(x) - n.start;
        grabPitch      = pitchAt(y) - n.pitch;
        drag           = Drag::Move;
    }
    imgui->wake();   // the inspector shows the selection
    repaint();
    return true;
}

bool NoteCanvas::onMotion(const MotionEvent& ev) {
    const bool taken = NanoTopLevelWidget::onMotion(ev);
    const float x = static_cast<float>(ev.pos.getX()) / sf(), y = static_cast<float>(ev.pos.getY()) / sf();
    const bool  inGrid = x >= gridX() && x < gridX() + gridW() && y >= gridY() && y < gridY() + gridH();
    const int   row    = !taken && inGrid ? pitchAt(y) : -1;
    if (row != hoverRow) {
        hoverRow = row;
        repaint();
    }
    if (taken) return true;
    ++probe.canvasMotions;
    if (drag == Drag::Move && model.selected >= 0) {
        Note& n = model.notes[static_cast<std::size_t>(model.selected)];
        n.start = std::clamp(stepAt(x) - grabStep, 0, model.steps - n.length);
        n.pitch = std::clamp(pitchAt(y) - grabPitch, 0, 127);
        imgui->wake();
        repaint();
        return true;
    }
    return false;
}

bool NoteCanvas::onScroll(const ScrollEvent& ev) {
    if (NanoTopLevelWidget::onScroll(ev)) return true;
    scrollY = std::clamp(scrollY - static_cast<float>(ev.delta.getY()) * kRowH * 3, 0.0f,
                         std::max(0.0f, 128 * kRowH - gridH()));
    repaint();
    return true;
}

bool NoteCanvas::onKeyboard(const KeyboardEvent& ev) {
    if (NanoTopLevelWidget::onKeyboard(ev)) return true;
    if (!ev.press) return false;
    ++probe.canvasKeys;
    const bool ctrl = (ev.mod & dgl::kModifierControl) != 0;
    if (ctrl && ev.key == 'q') {
        imgui->quitRequested = true;
    } else if ((ev.key == dgl::kKeyDelete || ev.key == dgl::kKeyBackspace) && model.selected >= 0) {
        model.deleteSelected();
    } else if ((ev.key == dgl::kKeyUp || ev.key == dgl::kKeyDown) && model.selected >= 0) {
        Note& n = model.notes[static_cast<std::size_t>(model.selected)];
        n.pitch = std::clamp(n.pitch + (ev.key == dgl::kKeyUp ? 1 : -1), 0, 127);
    } else {
        return false;
    }
    imgui->wake();
    repaint();
    return true;
}

void NoteCanvas::onResize(const ResizeEvent& ev) {
    NanoTopLevelWidget::onResize(ev);
    imgui->setSize(ev.size);   // the ImGui layer always covers the window
}

} // namespace umpire::spike
