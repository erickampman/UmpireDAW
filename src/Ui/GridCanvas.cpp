#include "Ui/GridCanvas.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "Ui/GridPanels.h"

namespace umpire::ui {

namespace dgl = DGL_NAMESPACE;

namespace {

// A row is dark when its pitch rounds to a 12-TET black key, as StageHand's
// sequence editor draws them: in 31-EDO the familiar 2+3 pattern shows as bands.
bool blackKey(double semitones) {
    const int pc = ((static_cast<int>(std::lround(semitones)) % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

std::string noteName(double semitones) {
    static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int n = static_cast<int>(std::lround(semitones));
    return names[((n % 12) + 12) % 12] + std::to_string(n / 12 - 1);
}

// The palette's colours (Widgets.h), as NanoVG's.
dgl::Color rgb(int r, int g, int b) { return dgl::Color(r, g, b); }

} // namespace

GridCanvas::GridCanvas(dgl::Window& window, GridModel& m, Probe& p) : NanoTopLevelWidget(window), model(m), probe(p) {
    loadSharedResources();   // DGL's built-in font (DejaVu Sans)
    imgui = std::make_unique<GridPanels>(this, model, probe);
    imgui->setSize(getWidth(), getHeight());
    // A clip's notes in the middle; an empty grid with C5 near the top.
    if (model.notes.empty()) {
        scrollY = rowY(model.rowFor(72 << 9)) - 40;
    } else {
        double sum = 0;
        for (const GridNote& n : model.notes) sum += n.pitch;
        centreOn(static_cast<int>(std::lround(sum / static_cast<double>(model.notes.size()))));
    }
    tunedEdo = model.edo, tunedReference = model.referenceNote;
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
    return static_cast<float>(model.rows() - 1 - pitch) * kRowH;
}
int GridCanvas::pitchAt(float y) {
    return model.rows() - 1 - static_cast<int>(std::floor((y - gridY() + scrollY) / kRowH));
}
int GridCanvas::stepAt(float x) {
    return static_cast<int>(std::floor((x - gridX()) / kStepW));
}
Tick GridCanvas::tickAt(float x) {
    return static_cast<Tick>(std::floor((x - gridX()) / kStepW * static_cast<float>(GridModel::kStep)));
}
float GridCanvas::tickX(Tick t) {
    return static_cast<float>(static_cast<double>(t) / GridModel::kStep) * kStepW;
}

int GridCanvas::edgeAt(float x, float y) {
    if (!inGrid(x, y)) return -1;
    const int row = pitchAt(y);
    for (int i = static_cast<int>(model.notes.size()) - 1; i >= 0; --i) {
        const GridNote& n   = model.notes[static_cast<std::size_t>(i)];
        const float     end = gridX() + tickX(n.start + n.length);
        // A short note keeps most of itself for moving.
        const float edge = std::min(kEdge, (tickX(n.length)) / 3);
        if (n.pitch == row && x <= end && x >= end - edge) return i;
    }
    return -1;
}

void GridCanvas::setPointer(dgl::MouseCursor cursor) {
    if (cursor == pointer) return;
    pointer = cursor;
    setCursor(cursor);
}

void GridCanvas::centreOn(int row) {
    scrollY = rowY(row) + kRowH / 2 - gridH() / 2;
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
    // A new tuning changes the rows: keep the pitch that was in the middle
    // there, and the scroll inside them.
    if (model.edo != tunedEdo || model.referenceNote != tunedReference) {
        centreOn(model.rowFor(static_cast<std::uint16_t>(std::lround(std::clamp(centrePitch, 0.0, 127.99) * 512))));
        tunedEdo = model.edo, tunedReference = model.referenceNote;
    }
    scrollY     = std::clamp(scrollY, 0.0f, std::max(0.0f, static_cast<float>(model.rows()) * kRowH - gh));
    centrePitch = model.rowSemitones(std::clamp(pitchAt(gy + gh / 2), 0, model.rows() - 1));

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
    // One row per EDO step; the octaves are labelled from the reference note.
    for (int pitch = model.rows() - 1; pitch >= 0; --pitch) {
        const float y = gy + rowY(pitch) - scrollY;
        if (y + kRowH < gy || y > gy + gh) continue;
        const double semitones = model.rowSemitones(pitch);
        beginPath();
        rect(gx, y, gw, kRowH);
        if (pitch == hoverRow) fillColor(rgb(48, 52, 62));
        else if (blackKey(semitones)) fillColor(rgb(24, 26, 31));
        else fillColor(rgb(31, 33, 39));
        fill();
        beginPath();   // the keyboard
        rect(0, y, gx - 1, kRowH - 1);
        if (blackKey(semitones)) fillColor(rgb(40, 42, 48));
        else fillColor(rgb(200, 202, 208));
        fill();
        if ((pitch - model.referenceRow()) % model.edo == 0) {
            fillColor(rgb(20, 20, 24));
            fontSize(10);
            text(3, y + kRowH / 2, noteName(semitones).c_str(), nullptr);
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
        const float     x = gx + tickX(n.start), y = gy + rowY(n.pitch) - scrollY;
        // The notes' teal, brighter when louder.
        const float t = 0.45f + 0.55f * static_cast<float>(n.velocity) / GridModel::kMaxVelocity;
        beginPath();
        roundedRect(x + 1, y + 1, std::max(tickX(n.length) - 2, 2.0f), kRowH - 2, 3);
        fillColor(rgb(static_cast<int>(72 * t), static_cast<int>(182 * t), static_cast<int>(196 * t)));
        fill();
        if (n.selected) {
            strokeColor(rgb(240, 170, 50));   // the accent's amber
            strokeWidth(static_cast<int>(i) == model.focus ? 2.0f : 1.5f);
            stroke();
        }
    }
    if (drag == Drag::Box) {   // the selection box, in the accent
        beginPath();
        rect(std::min(pressX, boxX), std::min(pressY, boxY), std::abs(boxX - pressX), std::abs(boxY - pressY));
        fillColor(dgl::Color(240, 170, 50, 0.12f));
        fill();
        strokeColor(rgb(240, 170, 50));
        strokeWidth(1);
        stroke();
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
        if (ev.button != dgl::kMouseButtonLeft || drag == Drag::None) return false;
        const Drag was = std::exchange(drag, Drag::None);
        if (was == Drag::Click) {   // a click on empty grid adds a note
            if (model.addNote(pitchAt(pressY), stepAt(pressX)) >= 0) model.commit();
        } else if (was == Drag::Move || was == Drag::Resize) {
            // A click on a note of a selection, without a drag, selects only it.
            if (!model.commit() && std::abs(x - pressX) < kSlop && std::abs(y - pressY) < kSlop)
                model.select(model.focus);
        }
        changed();
        return true;
    }
    if (!inGrid(x, y)) return false;
    const bool shift = (ev.mod & dgl::kModifierShift) != 0;
    const int  hit   = model.noteAt(pitchAt(y), tickAt(x));
    const int  edge  = edgeAt(x, y);
    if (ev.button == dgl::kMouseButtonRight) {
        // Deletes the note, or the selection it's in.
        if (hit < 0) return true;
        if (!model.notes[static_cast<std::size_t>(hit)].selected) model.select(hit);
        model.deleteSelected();
        model.commit();
    } else if (ev.button == dgl::kMouseButtonLeft) {
        pressX = x, pressY = y, shiftAtPress = shift;
        grabStep = stepAt(x), grabPitch = pitchAt(y), grabTick = tickAt(x);
        const int note = edge >= 0 ? edge : hit;
        if (note < 0) {
            drag = Drag::Click;
            if (shift) {
                boxBase.clear();
                for (const GridNote& n : model.notes) boxBase.push_back(n.selected);
            } else {
                model.select(-1);
            }
        } else if (shift) {
            model.toggle(note);   // in or out of the selection; no drag
        } else {
            if (!model.notes[static_cast<std::size_t>(note)].selected) model.select(note);
            else model.focus = note, model.touch();
            model.beginDrag();
            drag = edge >= 0 ? Drag::Resize : Drag::Move;
        }
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
    if (taken) {
        setPointer(dgl::kMouseCursorArrow);
        return true;
    }
    ++probe.canvasMotions;
    // Snapped to sixteenths, by the columns the pointer crossed; Shift: freely.
    const bool snap  = (ev.mod & dgl::kModifierShift) == 0;
    const Tick ticks = snap ? (stepAt(x) - grabStep) * GridModel::kStep : tickAt(x) - grabTick;
    switch (drag) {
    case Drag::Move: model.dragMove(ticks, pitchAt(y) - grabPitch, snap); break;
    case Drag::Resize: model.dragResize(ticks, snap); break;
    case Drag::Click:
        if (std::abs(x - pressX) < kSlop && std::abs(y - pressY) < kSlop) return true;
        drag = Drag::Box;
        [[fallthrough]];
    case Drag::Box: {
        boxX = x, boxY = y;
        if (shiftAtPress && boxBase.size() == model.notes.size())
            for (std::size_t i = 0; i < boxBase.size(); ++i) model.notes[i].selected = boxBase[i];
        model.selectIn(pitchAt(pressY), pitchAt(y), tickAt(pressX), tickAt(x), shiftAtPress);
        model.touch();
        break;
    }
    case Drag::None:
        setPointer(edgeAt(x, y) >= 0 ? dgl::kMouseCursorLeftRight : dgl::kMouseCursorArrow);
        return false;
    }
    changed();
    return true;
}

bool GridCanvas::onScroll(const ScrollEvent& ev) {
    if (NanoTopLevelWidget::onScroll(ev)) return true;
    scrollY = std::clamp(scrollY - static_cast<float>(ev.delta.getY()) * kRowH * 3, 0.0f,
                         std::max(0.0f, static_cast<float>(model.rows()) * kRowH - gridH()));
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
    const bool shift    = (ev.mod & dgl::kModifierShift) != 0;
    const bool selected = model.selectionSize() > 0 && drag == Drag::None;
    if (command && ev.key == 'q') {
        imgui->quitRequested = true;
    } else if (command && ((ev.key == 'z' && shift) || ev.key == 'y')) {
        if (model.redo) model.redo();
    } else if (command && ev.key == 'z') {
        if (model.undo) model.undo();
    } else if ((ev.key == dgl::kKeyDelete || ev.key == dgl::kKeyBackspace) && selected) {
        model.deleteSelected();
        model.commit();
    } else if ((ev.key == dgl::kKeyUp || ev.key == dgl::kKeyDown) && selected) {
        // A row, or with Shift an octave (the EDO's rows).
        const int by = shift ? model.edo : 1;
        model.nudge(0, ev.key == dgl::kKeyUp ? by : -by);
        model.commit();
    } else if ((ev.key == dgl::kKeyLeft || ev.key == dgl::kKeyRight) && selected) {
        model.nudge(ev.key == dgl::kKeyRight ? GridModel::kStep : -GridModel::kStep, 0);
        model.commit();
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
