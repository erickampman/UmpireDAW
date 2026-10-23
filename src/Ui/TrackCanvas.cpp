#include "Ui/TrackCanvas.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "Ui/TrackPanels.h"

namespace umpire::ui {

namespace dgl = DGL_NAMESPACE;

namespace {

dgl::Color rgb(int r, int g, int b, int a = 255) { return dgl::Color(r, g, b, a); }

} // namespace

TrackCanvas::TrackCanvas(dgl::Window& window, TrackController& c, Probe& p)
    : NanoTopLevelWidget(window), controller(c), probe(p) {
    loadSharedResources();   // DGL's built-in font (DejaVu Sans)
    imgui = std::make_unique<TrackPanels>(this, controller, probe);
    imgui->setSize(getWidth(), getHeight());
}

TrackCanvas::~TrackCanvas() = default;

float TrackCanvas::sf() {
    return static_cast<float>(getWindow().getScaleFactor());
}
float TrackCanvas::rulerY() {
    return imgui->menuBarHeight() / sf();
}
float TrackCanvas::lanesH() {
    return std::max(0.0f, (static_cast<float>(getHeight()) - imgui->monitorHeight()) / sf() - lanesY());
}
int TrackCanvas::rowAt(float y) {
    if (y < lanesY() || y >= lanesY() + lanesH()) return -1;
    const int i = static_cast<int>(std::floor((y - lanesY() + scrollY) / kRowH));
    return i < static_cast<int>(controller.view().rows.size()) ? i : -1;
}

void TrackCanvas::clampScroll() {
    const float content = static_cast<float>(controller.view().rows.size()) * kRowH;
    scrollY = std::clamp(scrollY, 0.0f, std::max(0.0f, content - lanesH()));
    scrollX = std::max(0.0f, scrollX);
}

void TrackCanvas::scrollBy(float rows) {
    const float before = scrollY;
    scrollY -= rows * kRowH;
    clampScroll();
    if (scrollY != before) refresh();
}

void TrackCanvas::refresh() {
    clampScroll();
    imgui->wake();
    repaint();
}

// --- drawing --------------------------------------------------------------------------

void TrackCanvas::onNanoDisplay() {
    const float s = sf();
    scale(s, s);
    const float W = static_cast<float>(getWidth()) / s, H = static_cast<float>(getHeight()) / s;
    const float x0 = kHeaderW, laneW = std::max(0.0f, W - x0);
    const float ry = rulerY(), ly = lanesY(), lh = lanesH();
    const TrackViewModel& view = controller.view();
    const int   beatsPerBar = view.beatsPerBar;

    beginPath();
    rect(0, 0, W, H);
    fillColor(rgb(17, 18, 22));   // the wells' graphite
    fill();

    // The ruler: bar numbers, and a tick per beat.
    beginPath();
    rect(x0, ry, laneW, kRulerH);
    fillColor(rgb(26, 28, 33));
    fill();
    save();
    scissor(x0, ry, laneW, kRulerH);
    fontSize(11);
    textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
    const int firstBeat = static_cast<int>(std::floor(scrollX / kBeatW));
    for (int beat = firstBeat;; ++beat) {
        const float x = x0 + static_cast<float>(beat) * kBeatW - scrollX;
        if (x > W) break;
        const bool bar = beat % beatsPerBar == 0;
        beginPath();
        moveTo(x, ry + (bar ? 4.0f : kRulerH - 6.0f));
        lineTo(x, ry + kRulerH);
        strokeWidth(1);
        strokeColor(bar ? rgb(150, 156, 170) : rgb(80, 85, 98));
        stroke();
        if (bar) {
            fillColor(rgb(170, 176, 190));
            text(x + 4, ry + kRulerH / 2 - 2, std::to_string(beat / beatsPerBar + 1).c_str(), nullptr);
        }
    }
    restore();
    beginPath();
    moveTo(0, ly - 0.5f);
    lineTo(W, ly - 0.5f);
    strokeColor(rgb(60, 64, 74));
    stroke();

    // The lanes: one per track, the selected one lit, with bar and beat lines.
    save();
    scissor(x0, ly, laneW, lh);
    for (std::size_t i = 0; i < view.rows.size(); ++i) {
        const TrackRowModel& row = view.rows[i];
        const float y = rowY(static_cast<int>(i));
        if (y + kRowH < ly || y > ly + lh) continue;
        beginPath();
        rect(x0, y, laneW, kRowH);
        if (row.selected) fillColor(rgb(36, 39, 46));
        else fillColor(i % 2 == 0 ? rgb(24, 26, 31) : rgb(21, 22, 27));
        fill();
        if (row.armed) {   // a red wash: what's played in will land here (milestone 6)
            beginPath();
            rect(x0, y, laneW, kRowH);
            fillColor(rgb(232, 72, 72, 18));
            fill();
        }
        beginPath();
        moveTo(x0, y + kRowH - 0.5f);
        lineTo(x0 + laneW, y + kRowH - 0.5f);
        strokeColor(rgb(40, 43, 51));
        stroke();
    }
    const float rowsBottom = std::min(ly + lh, rowY(static_cast<int>(view.rows.size())));
    for (int beat = firstBeat; rowsBottom > ly; ++beat) {
        const float x = x0 + static_cast<float>(beat) * kBeatW - scrollX;
        if (x > W) break;
        beginPath();
        moveTo(x, ly);
        lineTo(x, rowsBottom);
        strokeColor(beat % beatsPerBar == 0 ? rgb(60, 64, 74) : rgb(34, 37, 44));
        stroke();
    }
    restore();
}

// --- input ----------------------------------------------------------------------------
// The ImGui panels see each event first (TopLevelWidget's default); the
// canvas handles what's left.

bool TrackCanvas::onMouse(const MouseEvent& ev) {
    if (NanoTopLevelWidget::onMouse(ev)) return true;
    if (!ev.press || ev.button != dgl::kMouseButtonLeft) return false;
    const float x = static_cast<float>(ev.pos.getX()) / sf(), y = static_cast<float>(ev.pos.getY()) / sf();
    const int   row = x >= kHeaderW ? rowAt(y) : -1;
    if (row < 0) return false;
    controller.select(controller.view().rows[static_cast<std::size_t>(row)].id);
    refresh();
    return true;
}

bool TrackCanvas::onMotion(const MotionEvent& ev) {
    if (NanoTopLevelWidget::onMotion(ev)) return true;
    ++probe.canvasMotions;
    return false;
}

bool TrackCanvas::onScroll(const ScrollEvent& ev) {
    if (NanoTopLevelWidget::onScroll(ev)) return true;
    const bool sideways = (ev.mod & dgl::kModifierShift) != 0 || ev.delta.getX() != 0;
    if (sideways) {
        const double d = ev.delta.getX() != 0 ? ev.delta.getX() : ev.delta.getY();
        scrollX -= static_cast<float>(d) * kBeatW * 2;
        clampScroll();
        refresh();
    } else {
        scrollBy(static_cast<float>(ev.delta.getY()));
    }
    return true;
}

bool TrackCanvas::onKeyboard(const KeyboardEvent& ev) {
    if (NanoTopLevelWidget::onKeyboard(ev)) return true;
    if (!ev.press) return false;
#ifdef __APPLE__
    const bool command = (ev.mod & dgl::kModifierSuper) != 0;   // Cmd
#else
    const bool command = (ev.mod & dgl::kModifierControl) != 0;
#endif
    const TrackViewModel& view = controller.view();
    int selected = -1;
    for (std::size_t i = 0; i < view.rows.size(); ++i)
        if (view.rows[i].selected) selected = static_cast<int>(i);
    if (command && ev.key == 'q') {
        imgui->quitRequested = true;
    } else if (command && ev.key == 't') {
        controller.addTrack();
    } else if ((ev.key == dgl::kKeyUp || ev.key == dgl::kKeyDown) && !view.rows.empty()) {
        const int next = std::clamp(selected + (ev.key == dgl::kKeyUp ? -1 : 1), 0, static_cast<int>(view.rows.size()) - 1);
        controller.select(view.rows[static_cast<std::size_t>(next)].id);
    } else if (ev.key == 'r' && selected >= 0) {   // arm, as most DAWs
        controller.setArmed(view.rows[static_cast<std::size_t>(selected)].id, !view.rows[static_cast<std::size_t>(selected)].armed);
    } else {
        return false;
    }
    refresh();
    return true;
}

void TrackCanvas::onResize(const ResizeEvent& ev) {
    NanoTopLevelWidget::onResize(ev);
    imgui->setSize(ev.size);   // the ImGui panels always cover the window
    clampScroll();
}

} // namespace umpire::ui
