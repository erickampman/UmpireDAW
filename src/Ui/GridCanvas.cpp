#include "Ui/GridCanvas.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "Ui/GridPanels.h"

namespace umpire::ui {

namespace dgl = DGL_NAMESPACE;

namespace {

bool blackKey(int pitch) {
    const int pc = ((pitch % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

// The palette's colours (Widgets.h), as NanoVG's.
dgl::Color rgb(int r, int g, int b) { return dgl::Color(r, g, b); }

} // namespace

GridCanvas::GridCanvas(dgl::Window& window, GridModel& m, Probe& p) : NanoTopLevelWidget(window), model(m), probe(p) {
    loadSharedResources();   // DGL's built-in font (DejaVu Sans)
    imgui = std::make_unique<GridPanels>(this, model, probe);
    imgui->setSize(getWidth(), getHeight());
    scrollY = rowY(72) - 40;   // C5 near the top
}

GridCanvas::~GridCanvas() = default;

float GridCanvas::sf() {
    return static_cast<float>(getWindow().getScaleFactor());
}
float GridCanvas::gridY() {
    return imgui->menuBarHeight() / sf() + 20;   // a strip for the clip name
}
float GridCanvas::gridW() {
    return std::max(0.0f, (static_cast<float>(getWidth()) - imgui->panelWidth()) / sf() - gridX());
}
float GridCanvas::gridH() {
    return std::max(0.0f, static_cast<float>(getHeight()) / sf() - gridY());
}
bool GridCanvas::inGrid(float x, float y) {
    return x >= gridX() && x < gridX() + gridW() && y >= gridY() && y < gridY() + gridH();
}
float GridCanvas::rowY(int pitch) {
    return static_cast<float>(GridModel::kPitches - 1 - pitch) * kRowH;
}
int GridCanvas::pitchAt(float y) {
    return GridModel::kPitches - 1 - static_cast<int>(std::floor((y - gridY() + scrollY) / kRowH));
}
int GridCanvas::stepAt(float x) {
    return static_cast<int>(std::floor((x - gridX()) / kStepW));
}

void GridCanvas::changed() {
    imgui->wake();   // the inspector shows the selection
    repaint();
}

// --- drawing --------------------------------------------------------------------------

void GridCanvas::onNanoDisplay() {
    const float s = sf();
    scale(s, s);
    const float W = static_cast<float>(getWidth()) / s, H = static_cast<float>(getHeight()) / s;
    const float gx = gridX(), gy = gridY(), gw = gridW(), gh = gridH();

    beginPath();
    rect(0, 0, W, H);
    fillColor(rgb(17, 18, 22));   // the wells' graphite
    fill();

    fontSize(13);
    textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
    fillColor(rgb(150, 156, 170));
    text(gx, gy - 10, model.clipName.c_str(), nullptr);

    save();
    scissor(0, gy, gx + gw, gh);
    for (int pitch = GridModel::kPitches - 1; pitch >= 0; --pitch) {
        const float y = gy + rowY(pitch) - scrollY;
        if (y + kRowH < gy || y > gy + gh) continue;
        beginPath();
        rect(gx, y, gw, kRowH);
        if (pitch == hoverRow) fillColor(rgb(48, 52, 62));
        else if (blackKey(pitch)) fillColor(rgb(24, 26, 31));
        else fillColor(rgb(31, 33, 39));
        fill();
        beginPath();   // the keyboard
        rect(0, y, gx - 1, kRowH - 1);
        if (blackKey(pitch)) fillColor(rgb(40, 42, 48));
        else fillColor(rgb(200, 202, 208));
        fill();
        if (pitch % 12 == 0) {
            fillColor(rgb(20, 20, 24));
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
        if (step % 16 == 0) strokeColor(rgb(104, 110, 124));
        else if (step % 4 == 0) strokeColor(rgb(60, 64, 74));
        else strokeColor(rgb(42, 45, 53));
        stroke();
    }
    for (std::size_t i = 0; i < model.notes.size(); ++i) {
        const GridNote& n = model.notes[i];
        const float     x = gx + static_cast<float>(n.start) * kStepW, y = gy + rowY(n.pitch) - scrollY;
        // The notes' teal, brighter when louder.
        const float t = 0.45f + 0.55f * static_cast<float>(n.velocity) / GridModel::kMaxVelocity;
        beginPath();
        roundedRect(x + 1, y + 1, static_cast<float>(n.length) * kStepW - 2, kRowH - 2, 3);
        fillColor(rgb(static_cast<int>(72 * t), static_cast<int>(182 * t), static_cast<int>(196 * t)));
        fill();
        if (static_cast<int>(i) == model.selected) {
            strokeColor(rgb(240, 170, 50));   // the accent's amber
            strokeWidth(2);
            stroke();
        }
    }
    restore();
}

// --- input ----------------------------------------------------------------------------
// Each handler offers the event to the ImGui panels first (TopLevelWidget's
// default passes it to the subwidgets); the canvas handles what's left.

bool GridCanvas::onMouse(const MouseEvent& ev) {
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
    if (!inGrid(x, y)) return false;
    int hit = model.noteAt(pitchAt(y), stepAt(x));
    if (ev.button == dgl::kMouseButtonRight) {
        if (hit >= 0) {
            model.select(hit);
            model.deleteSelected();
        }
    } else if (ev.button == dgl::kMouseButtonLeft) {
        if (hit < 0) hit = model.addNote(pitchAt(y), stepAt(x));
        if (hit < 0) return true;   // past the clip's end
        model.select(hit);
        const GridNote& n = model.notes[static_cast<std::size_t>(hit)];
        grabStep          = stepAt(x) - n.start;
        grabPitch         = pitchAt(y) - n.pitch;
        drag              = Drag::Move;
    }
    changed();
    return true;
}

bool GridCanvas::onMotion(const MotionEvent& ev) {
    const bool  taken = NanoTopLevelWidget::onMotion(ev);
    const float x = static_cast<float>(ev.pos.getX()) / sf(), y = static_cast<float>(ev.pos.getY()) / sf();
    const int   row = !taken && inGrid(x, y) ? pitchAt(y) : -1;
    if (row != hoverRow) {
        hoverRow = row;
        repaint();
    }
    if (taken) return true;
    ++probe.canvasMotions;
    if (drag == Drag::Move && model.selectedNote() != nullptr) {
        model.moveSelected(stepAt(x) - grabStep, pitchAt(y) - grabPitch);
        changed();
        return true;
    }
    return false;
}

bool GridCanvas::onScroll(const ScrollEvent& ev) {
    if (NanoTopLevelWidget::onScroll(ev)) return true;
    scrollY = std::clamp(scrollY - static_cast<float>(ev.delta.getY()) * kRowH * 3, 0.0f,
                         std::max(0.0f, GridModel::kPitches * kRowH - gridH()));
    repaint();
    return true;
}

bool GridCanvas::onKeyboard(const KeyboardEvent& ev) {
    if (NanoTopLevelWidget::onKeyboard(ev)) return true;
    if (!ev.press) return false;
#ifdef __APPLE__
    const bool command = (ev.mod & dgl::kModifierSuper) != 0;   // Cmd
#else
    const bool command = (ev.mod & dgl::kModifierControl) != 0;
#endif
    if (command && ev.key == 'q') {
        imgui->quitRequested = true;
    } else if ((ev.key == dgl::kKeyDelete || ev.key == dgl::kKeyBackspace) && model.selectedNote() != nullptr) {
        model.deleteSelected();
    } else if ((ev.key == dgl::kKeyUp || ev.key == dgl::kKeyDown) && model.selectedNote() != nullptr) {
        model.transposeSelected(ev.key == dgl::kKeyUp ? 1 : -1);
    } else {
        return false;
    }
    changed();
    return true;
}

void GridCanvas::onResize(const ResizeEvent& ev) {
    NanoTopLevelWidget::onResize(ev);
    imgui->setSize(ev.size);   // the ImGui panels always cover the window
}

} // namespace umpire::ui
