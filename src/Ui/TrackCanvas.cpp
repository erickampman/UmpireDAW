#include "Ui/TrackCanvas.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "Ui/TrackPanels.h"

namespace umpire::ui {

namespace dgl = DGL_NAMESPACE;

namespace {

// Alpha 0-255, as the rest; DGL takes it as 0-1.
dgl::Color rgb(int r, int g, int b, int a = 255) { return dgl::Color(r, g, b, static_cast<float>(a) / 255); }

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
    return std::max(0.0f, (static_cast<float>(getHeight()) - imgui->monitorHeight() - imgui->statusHeight()) / sf() - lanesY());
}
int TrackCanvas::rowAt(float y) {
    if (y < lanesY() || y >= lanesY() + lanesH()) return -1;
    const int i = static_cast<int>(std::floor((y - lanesY() + scrollY) / kRowH));
    return i < static_cast<int>(controller.view().rows.size()) ? i : -1;
}

ClipRefId TrackCanvas::clipAt(float x, float y) {
    const int row = x >= kHeaderW ? rowAt(y) : -1;
    if (row < 0) return 0;
    const auto& clips = controller.view().rows[static_cast<std::size_t>(row)].clips;
    for (auto it = clips.rbegin(); it != clips.rend(); ++it)   // the last drawn is on top
        if (it->ref != 0 && x >= tickX(it->start) && x < std::max(tickX(it->start + it->length), tickX(it->start) + 4))
            return it->ref;
    return 0;
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
    // Playing past the right edge turns the page.
    const TransportModel& tr = controller.view().transport;
    const float laneW = static_cast<float>(getWidth()) / sf() - kHeaderW;
    if (tr.playing && laneW > kBeatW * 4 && tickX(tr.position) > kHeaderW + laneW - kBeatW)
        scrollX += laneW - kBeatW * 2;
    clampScroll();
    // While playing the view changes about 30 times a second (the playhead):
    // one frame each is enough, not the few ImGui is owed after input.
    imgui->wake(tr.playing ? 1 : 3);
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
    // The loop, across the ruler: amber while looping, grey when off; the
    // range being dragged, brighter.
    {
        const TransportModel& tr = view.transport;
        const Tick from = loopDrag ? std::min(loopFrom, loopTo) : tr.loopStart;
        const Tick to   = loopDrag ? std::max(loopFrom, loopTo) : tr.loopEnd;
        if (to > from) {
            const bool on = loopDrag || tr.looping;
            beginPath();
            rect(tickX(from), ry + 2, tickX(to) - tickX(from), kRulerH - 4);
            fillColor(on ? rgb(240, 170, 50, loopDrag ? 110 : 70) : rgb(110, 116, 130, 50));
            fill();
            beginPath();
            moveTo(tickX(from) + 0.5f, ry + 2);
            lineTo(tickX(from) + 0.5f, ry + kRulerH - 2);
            moveTo(tickX(to) - 0.5f, ry + 2);
            lineTo(tickX(to) - 0.5f, ry + kRulerH - 2);
            strokeWidth(1.5f);
            strokeColor(on ? rgb(240, 170, 50) : rgb(110, 116, 130));
            stroke();
        }
    }
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
        if (row.touched) fillColor(rgb(36, 39, 46));
        else fillColor(i % 2 == 0 ? rgb(24, 26, 31) : rgb(21, 22, 27));
        fill();
        if (row.armed) {   // a red wash: what's recorded lands here
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
    for (std::size_t i = 0; i < view.rows.size(); ++i) {
        const float y = rowY(static_cast<int>(i));
        if (y + kRowH < ly || y > ly + lh) continue;
        for (const ClipModel& clip : view.rows[i].clips) drawClip(clip, y, W);
    }
    restore();

    // An empty session: only the prompt (ui-direction.md, the first mockup).
    if (view.rows.empty()) {
        const float cx = x0 + laneW / 2, cy = ly + std::min(lh / 2, 160.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fontSize(20);
        fillColor(rgb(200, 204, 214));
        text(cx, cy, "Play something to start", nullptr);
        fontSize(13);
        fillColor(rgb(110, 116, 130));
#ifdef __APPLE__
        text(cx, cy + 26, "or Cmd+T to add a track", nullptr);
#else
        text(cx, cy + 26, "or Ctrl+T to add a track", nullptr);
#endif
    }

    // The playhead, over the ruler and the lanes: red while recording.
    const TransportModel& tr = view.transport;
    const float px = tickX(tr.position);
    if (px >= x0 && px <= W) {
        save();
        scissor(x0, ry, laneW, kRulerH + lh);
        const dgl::Color c = tr.recording ? rgb(232, 72, 72) : rgb(240, 170, 50);
        beginPath();
        moveTo(px, ry);
        lineTo(px, ly + lh);
        strokeWidth(1.5f);
        strokeColor(c);
        stroke();
        beginPath();
        moveTo(px - 5, ry);
        lineTo(px + 5, ry);
        lineTo(px, ry + 7);
        closePath();
        fillColor(c);
        fill();
        restore();
    }
}

// A clip on its lane: a box from its start to its end, its name, and its notes
// as dashes between the lowest pitch and the highest, wherever they fall (no
// tuning assumed: the pitch is the note's 7.9). A take is red; the selected
// clip has the accent round it.
void TrackCanvas::drawClip(const ClipModel& clip, float y, float laneRight) {
    const Tick  start = clip.ref != 0 && clip.ref == dragRef ? dragTo : clip.start;
    const float x0 = tickX(start), x1 = std::max(tickX(start + clip.length), x0 + 4);
    if (x1 < kHeaderW || x0 > laneRight) return;
    const float top = y + 4, h = kRowH - 8;
    const dgl::Color body = clip.recording ? rgb(232, 72, 72, 60) : rgb(72, 182, 196, 46);
    const dgl::Color edge = clip.recording ? rgb(232, 72, 72) : clip.selected ? rgb(240, 170, 50) : rgb(72, 182, 196, 150);
    const dgl::Color ink  = clip.recording ? rgb(255, 170, 170) : rgb(150, 220, 230);
    beginPath();
    roundedRect(x0, top, x1 - x0, h, 3);
    fillColor(body);
    fill();
    strokeWidth(clip.selected ? 2.0f : 1.0f);
    strokeColor(edge);
    stroke();

    save();
    intersectScissor(x0, top, x1 - x0, h);
    fontSize(11);
    textAlign(ALIGN_LEFT | ALIGN_TOP);
    fillColor(ink);
    text(x0 + 4, top + 2, clip.name.c_str(), nullptr);
    const float notesTop = top + 15, notesH = h - 18;
    // At least an octave tall, centred on what's there; a dash an octave's
    // twelfth high.
    constexpr float octave = 12 * 512;
    const float span = std::max(static_cast<float>(clip.highPitch - clip.lowPitch), octave);
    const float low  = static_cast<float>(clip.lowPitch) - (span - static_cast<float>(clip.highPitch - clip.lowPitch)) / 2;
    const float dash = std::max(notesH * octave / 12 / (span + octave / 12), 1.5f);
    if (clip.notes) {
        beginPath();
        for (const NoteSpan& n : *clip.notes) {
            const float nx = tickX(start + n.start);
            const float nw = std::max(static_cast<float>(n.length) / kTicksPerQuarter * kBeatW, 1.5f);
            const float ny = notesTop + (notesH - dash) * (1.0f - (static_cast<float>(n.pitch79) - low) / span);
            rect(nx, ny, nw, dash);
        }
        fillColor(ink);
        fill();
    }
    restore();
}

// --- input ----------------------------------------------------------------------------
// The ImGui panels see each event first (TopLevelWidget's default); the
// canvas handles what's left.

bool TrackCanvas::onMouse(const MouseEvent& ev) {
    if (NanoTopLevelWidget::onMouse(ev)) return true;
    if (ev.button != dgl::kMouseButtonLeft) return false;
    const float x = static_cast<float>(ev.pos.getX()) / sf(), y = static_cast<float>(ev.pos.getY()) / sf();
    if (!ev.press) {   // the end of a drag: the move, or the loop, as one command
        if (loopDrag) {
            loopDrag = false;
            if (loopTo != loopFrom) controller.setLoop(std::min(loopFrom, loopTo), std::max(loopFrom, loopTo));
            refresh();
            return true;
        }
        if (dragRef == 0) return false;
        const ClipRefId ref = dragRef;
        dragRef = 0;
        if (dragMoved && dragTo != dragOrigin) controller.moveClip(ref, dragTo);
        refresh();
        return true;
    }
    // The ruler: the playhead goes there, to the beat; a drag from there
    // sets the loop.
    if (x >= kHeaderW && y >= rulerY() && y < lanesY()) {
        const Tick at   = xTick(x) + kTicksPerQuarter / 2;
        const Tick beat = std::max<Tick>(0, at - at % kTicksPerQuarter);
        controller.locate(beat);
        loopDrag = true;
        loopFrom = loopTo = beat;
        refresh();
        return true;
    }
    const int row = x >= kHeaderW ? rowAt(y) : -1;
    if (row < 0) return false;
    const ClipRefId ref = clipAt(x, y);
    if (ref == 0) {
        controller.select(controller.view().rows[static_cast<std::size_t>(row)].id);
        controller.selectClip(0);
        lastPressRef = 0;
        refresh();
        return true;
    }
    // A double click on a clip opens it; a press selects it and may drag it.
    if (ref == lastPressRef && ev.time - lastPressTime < 400) {
        openRequested = ref;
        lastPressRef  = 0;
        return true;
    }
    lastPressRef  = ref;
    lastPressTime = ev.time;
    controller.selectClip(ref);
    for (const ClipModel& c : controller.view().rows[static_cast<std::size_t>(row)].clips)
        if (c.ref == ref) dragOrigin = dragTo = c.start;
    dragRef   = ref;
    dragGrab  = xTick(x) - dragOrigin;
    dragMoved = false;
    refresh();
    return true;
}

bool TrackCanvas::onMotion(const MotionEvent& ev) {
    if (NanoTopLevelWidget::onMotion(ev)) return true;
    ++probe.canvasMotions;
    if (loopDrag) {
        const Tick at   = xTick(static_cast<float>(ev.pos.getX()) / sf()) + kTicksPerQuarter / 2;
        const Tick beat = std::max<Tick>(0, at - ((at % kTicksPerQuarter) + kTicksPerQuarter) % kTicksPerQuarter);
        if (beat != loopTo) {
            loopTo = beat;
            refresh();
        }
        return true;
    }
    if (dragRef == 0) return false;
    Tick to = xTick(static_cast<float>(ev.pos.getX()) / sf()) - dragGrab;
    if ((ev.mod & dgl::kModifierShift) == 0) {   // to the nearest beat
        to += kTicksPerQuarter / 2;
        to -= ((to % kTicksPerQuarter) + kTicksPerQuarter) % kTicksPerQuarter;
    }
    to = std::max<Tick>(0, to);
    if (to != dragTo) {
        dragTo    = to;
        dragMoved = true;
        refresh();
    }
    return true;
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
    const bool shift = (ev.mod & dgl::kModifierShift) != 0;
    const TrackViewModel& view = controller.view();
    int touched = -1;
    for (std::size_t i = 0; i < view.rows.size(); ++i)
        if (view.rows[i].touched) touched = static_cast<int>(i);
    if (command && ev.key == 'q') {
        imgui->quitRequested = true;
    } else if (command && ev.key == 't') {
        controller.addTrack();
    } else if (command && ev.key == 'z') {
        shift ? controller.redo() : controller.undo();
    } else if (command && ev.key == 'y') {
        controller.redo();
    } else if (command && ev.key == 'r') {
        controller.record();
    } else if (ev.key == ' ') {
        view.transport.playing ? controller.stop() : controller.play();
    } else if (ev.key == dgl::kKeyHome) {
        controller.locate(0);
    } else if (ev.key == 'l' && !command) {
        controller.setLooping(!view.transport.looping);
    } else if ((ev.key == dgl::kKeyDelete || ev.key == dgl::kKeyBackspace) && controller.selectedClip() != 0) {
        controller.deleteClip(controller.selectedClip());
    } else if ((ev.key == dgl::kKeyUp || ev.key == dgl::kKeyDown) && !view.rows.empty()) {
        const int next = std::clamp(touched + (ev.key == dgl::kKeyUp ? -1 : 1), 0, static_cast<int>(view.rows.size()) - 1);
        controller.select(view.rows[static_cast<std::size_t>(next)].id);
    } else if (ev.key == 'r' && !command && touched >= 0) {   // arm, as most DAWs
        controller.setArmed(view.rows[static_cast<std::size_t>(touched)].id, !view.rows[static_cast<std::size_t>(touched)].armed);
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
