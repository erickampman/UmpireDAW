#include "Plugin/PluginEditor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <utility>
#include <vector>

#include "Application.hpp"
#include "Window.hpp"

#include "imgui.h"

#include "App/GridModel.h"
#include "Plugin/Lane.h"
#include "Plugin/LaneKeeper.h"
#include "Plugin/PluginEngine.h"
#include "Ui/GridCanvas.h"
#include "Ui/GridPanels.h"
#include "Ui/Probe.h"
#include "Ui/Widgets.h"

namespace umpire::plugin {

namespace dgl = DGL_NAMESPACE;
namespace col = ui::colour;

namespace {

// The grid's bars are 4/4 (sixteen of its sixteenths), and so are the lane's.
constexpr Tick kBar = 4 * kTicksPerQuarter;

Tick barStart(Tick t) { return t >= 0 ? t / kBar * kBar : -((-t + kBar - 1) / kBar) * kBar; }

dgl::Color rgb(int r, int g, int b, float a = 1.0f) { return dgl::Color(r, g, b, a); }

// "bar.beat.sixteenth", from 1, as hosts count.
std::string position(Tick t) {
    if (t < 0) return "-";
    const Tick bar = t / kBar, beat = t % kBar / kTicksPerQuarter, sixteenth = t % kTicksPerQuarter / (kTicksPerQuarter / 4);
    char text[48];
    std::snprintf(text, sizeof text, "%lld.%lld.%lld", static_cast<long long>(bar + 1), static_cast<long long>(beat + 1),
                  static_cast<long long>(sixteenth + 1));
    return text;
}

} // namespace

// --- the lane: the takes in the host's time, above the grid ----------------------------

// Laid out from the band's top: the ruler, the takes heard (newest on top),
// and a strip for the parts of older takes that newer ones cover.
class LaneBand final : public ui::GridBand {
public:
    static constexpr float kRulerH = 16, kTakeH = 40, kStripH = 10, kGap = 3, kBeatW = 16;
    static constexpr float kHeight = kRulerH + kGap + kTakeH + kGap + kStripH + 6;

    // The editor's state the band reads and changes.
    LaneKeeper::View*    view      = nullptr;
    ClipRefId*           selected  = nullptr;
    std::function<void(ClipRefId)> select;      // a take clicked (0: none)
    std::function<void(ClipRefId)> contextMenu; // a take right-clicked
    std::function<Tick()> playheadSong;         // the host's position, song ticks
    std::function<bool()> hostPlaying;
    Tick                  gridBar = 0;          // the grid's first tick, in the song

    float height(float) override { return kHeight; }
    bool  gridShown() override { return *selected != 0; }
    Tick  playhead() override { return gridShown() ? playheadSong() - gridBar : -1; }
    bool  playing() override { return hostPlaying(); }

    void draw(dgl::NanoVG& vg, float width, float h) override {
        // Paging, as the grid's: while the host plays, the playhead stays in view.
        const bool rolling = hostPlaying();
        if (rolling && !wasPlaying) handScrolled = false;
        wasPlaying = rolling;
        const float phX = tickX(playheadSong());
        if (rolling && !handScrolled && (phX < scrollX || phX >= scrollX + width)) scrollX = phX - kBeatW;
        clampScroll(width);

        vg.beginPath();
        vg.rect(0, 0, width, h);
        vg.fillColor(rgb(22, 23, 28));
        vg.fill();

        // The ruler: the host's bars.
        vg.fontSize(11);
        vg.textAlign(dgl::NanoVG::ALIGN_LEFT | dgl::NanoVG::ALIGN_MIDDLE);
        const Tick first = barStart(tickAt(0)), last = tickAt(width);
        for (Tick t = first; t <= last; t += kBar) {
            const float x = tickX(t) - scrollX;
            vg.beginPath();
            vg.moveTo(x, 0);
            vg.lineTo(x, kHeight);
            vg.strokeWidth(1);
            vg.strokeColor(rgb(52, 56, 66));
            vg.stroke();
            vg.fillColor(rgb(130, 136, 150));
            vg.text(x + 3, kRulerH / 2, std::to_string(t / kBar + 1).c_str(), nullptr);
        }

        const std::vector<ClipRef>& takes = view->takes;
        if (takes.empty()) {
            vg.fontSize(13);
            vg.fillColor(rgb(150, 156, 170));
            vg.text(12, kRulerH + kGap + kTakeH / 2, "Turn on Record and record in the host to make a take.", nullptr);
        }
        const float takeY = kRulerH + kGap, stripY = takeY + kTakeH + kGap;
        // The covered parts first, oldest first, as a strip under the takes.
        for (std::size_t i = 0; i < takes.size(); ++i) {
            const ClipRef& t = takes[i];
            Tick           from = t.start;
            const auto     spans = visibleSpans(takes, i);
            const auto     strip = [&](Tick a, Tick b) {
                if (b <= a) return;
                const float x0 = tickX(a) - scrollX, x1 = tickX(b) - scrollX;
                vg.beginPath();
                vg.roundedRect(x0, stripY, std::max(x1 - x0, 2.0f), kStripH, 2);
                vg.fillColor(rgb(58, 62, 72));
                vg.fill();
                vg.strokeWidth(t.id == *selected ? 1.5f : 0.5f);
                vg.strokeColor(t.id == *selected ? rgb(240, 170, 50) : rgb(90, 96, 110));
                vg.stroke();
            };
            for (const Span& s : spans) {
                strip(from, s.start);
                from = s.end;
            }
            strip(from, takeEnd(t));
        }
        // Then what's heard of each.
        for (std::size_t i = 0; i < takes.size(); ++i) {
            const ClipRef& t = takes[i];
            const bool     chosen = t.id == *selected;
            const auto&    sketch = notesFor(t);
            bool           named  = false;
            for (const Span& s : visibleSpans(takes, i)) {
                const float x0 = tickX(s.start) - scrollX, x1 = tickX(s.end) - scrollX;
                if (x1 < 0 || x0 > width) continue;
                vg.save();
                vg.beginPath();
                vg.roundedRect(x0, takeY, std::max(x1 - x0, 2.0f), kTakeH, 3);
                vg.fillColor(chosen ? rgb(24, 92, 100) : rgb(18, 70, 78));
                vg.fill();
                vg.strokeWidth(chosen ? 2.0f : 0.5f);
                vg.strokeColor(chosen ? rgb(240, 170, 50) : rgb(72, 182, 196));
                vg.stroke();
                vg.intersectScissor(x0, takeY, x1 - x0, kTakeH);
                // Its notes, sketched over its pitch range.
                const float top = takeY + 14, span = kTakeH - 18;
                for (const Sketch& n : sketch.notes) {
                    const float nx = tickX(t.start + n.start) - scrollX;
                    const float ny = top + span * (1.0f - (n.pitch - sketch.low) / std::max(1.0f, sketch.high - sketch.low));
                    vg.beginPath();
                    vg.rect(nx, ny - 1, std::max(tickX(n.length), 1.5f), 2.5f);
                    vg.fillColor(rgb(110, 210, 222));
                    vg.fill();
                }
                if (!named) {
                    vg.fontSize(11);
                    vg.fillColor(rgb(200, 236, 240));
                    vg.text(x0 + 4, takeY + 7, t.clip->name.c_str(), nullptr);
                    named = true;
                }
                vg.restore();
            }
        }

        // The host's playhead.
        const float x = phX - scrollX;
        vg.beginPath();
        vg.moveTo(x, 0);
        vg.lineTo(x, kHeight);
        vg.strokeWidth(1.5f);
        vg.strokeColor(rgb(240, 170, 50));
        vg.stroke();

        if (!takes.empty() && *selected == 0) {
            vg.fontSize(13);
            vg.fillColor(rgb(150, 156, 170));
            vg.text(12, kHeight + 24, "Click a take to edit its notes. Right-click it to bring it to the front or delete it.",
                    nullptr);
        }
    }

    bool mouse(const dgl::Widget::MouseEvent& ev, float x, float y) override {
        if (!ev.press) return true;
        const Tick tick = tickAt(x);
        ClipRefId  hit  = 0;
        if (y >= kRulerH + kGap && y < kRulerH + kGap + kTakeH) {
            hit = heardAt(tick);
        } else if (y >= kRulerH + kGap + kTakeH && y < kHeight) {
            // The strip: the covered takes here, newest first; another click
            // goes to the next older.
            std::vector<ClipRefId> covered = coveredAt(tick);
            if (!covered.empty()) {
                const auto at = std::find(covered.begin(), covered.end(), *selected);
                hit = at == covered.end() || at + 1 == covered.end() ? covered.front() : *(at + 1);
            }
        }
        if (ev.button == dgl::kMouseButtonRight) {
            if (hit != 0) {
                select(hit);
                contextMenu(hit);
            }
            return true;
        }
        if (ev.button == dgl::kMouseButtonLeft && y >= kRulerH) select(hit);
        return true;
    }

    bool scroll(const dgl::Widget::ScrollEvent& ev, float, float) override {
        // Either way through time, a beat a notch.
        const double d = ev.delta.getX() != 0 ? ev.delta.getX() : ev.delta.getY();
        scrollX -= static_cast<float>(d) * kBeatW * 4;
        handScrolled = true;
        return true;
    }

private:
    struct Sketch {
        Tick  start = 0, length = 0;
        float pitch = 60;
    };
    struct Sketches {
        std::vector<Sketch> notes;
        float               low = 60, high = 72;
    };

    float tickX(Tick t) const { return static_cast<float>(static_cast<double>(t) / kTicksPerQuarter) * kBeatW; }
    Tick  tickAt(float x) const {
        return static_cast<Tick>(std::floor((x + scrollX) / kBeatW * static_cast<float>(kTicksPerQuarter)));
    }
    void clampScroll(float width) {
        Tick lo = 0, hi = kBar * 8;
        for (const ClipRef& t : view->takes) lo = std::min(lo, t.start), hi = std::max(hi, takeEnd(t) + kBar * 4);
        hi = std::max(hi, playheadSong() + kBar * 4);
        scrollX = std::clamp(scrollX, tickX(barStart(lo)), std::max(tickX(barStart(lo)), tickX(hi) - width));
    }

    ClipRefId heardAt(Tick tick) const {
        const auto& takes = view->takes;
        for (std::size_t i = takes.size(); i-- > 0;)
            for (const Span& s : visibleSpans(takes, i))
                if (tick >= s.start && tick < s.end) return takes[i].id;
        return 0;
    }
    std::vector<ClipRefId> coveredAt(Tick tick) const {
        const ClipRefId        heard = heardAt(tick);
        std::vector<ClipRefId> out;
        const auto&            takes = view->takes;
        for (std::size_t i = takes.size(); i-- > 0;)
            if (takes[i].id != heard && tick >= takes[i].start && tick < takeEnd(takes[i])) out.push_back(takes[i].id);
        return out;
    }

    const Sketches& notesFor(const ClipRef& t) {
        auto it = sketches.find(t.clip.get());
        if (it != sketches.end()) return it->second;
        if (sketches.size() > 256) sketches.clear(), keep.clear();   // old clips' (edits, undo)
        Sketches s;
        bool     any = false;
        for (const NoteSpan& n : notesOf(t.clip->events, t.clip->length)) {
            const float p = static_cast<float>(n.pitch79) / 512.0f;
            s.notes.push_back({n.start, n.length, p});
            s.low  = any ? std::min(s.low, p) : p;
            s.high = any ? std::max(s.high, p) : p;
            any    = true;
        }
        if (s.high - s.low < 12) {   // at least an octave, centred
            const float mid = (s.low + s.high) / 2;
            s.low = mid - 6, s.high = mid + 6;
        }
        keep[t.clip.get()] = t.clip;   // so the address can't be another clip's
        return sketches.emplace(t.clip.get(), std::move(s)).first->second;
    }

    std::map<const Clip*, Sketches>                    sketches;
    std::map<const Clip*, std::shared_ptr<const Clip>> keep;
    float scrollX      = -kBeatW;
    bool  handScrolled = false, wasPlaying = false;
};

// --- the window -------------------------------------------------------------------------

struct PluginEditor::Impl {
    Impl(std::uintptr_t parent, unsigned w, unsigned h, double s, EditorLinks l)
        : links(std::move(l)), scale(s), app(false),
          window(app, parent, static_cast<uint>(std::lround(w * s)), static_cast<uint>(std::lround(h * s)), s, true) {}

    ~Impl() {
        const dgl::Window::ScopedGraphicsContext context(window);
        canvas.reset();
    }

    void build();
    void follow(bool force = false);
    void choose(ClipRefId ref);
    void showTake(const ClipRef& take);
    const ClipRef* selectedTake() const {
        for (const ClipRef& t : view.takes)
            if (t.id == selected) return &t;
        return nullptr;
    }
    // ImGui: the header's controls and the take's menus.
    void record();
    void synthButton();
    void status();
    void takeItems();
    void takePopup();

    EditorLinks      links;
    double           scale;
    dgl::Application app;
    dgl::Window      window;
    GridModel        model;
    ui::Probe        probe;
    LaneBand         band;
    std::unique_ptr<ui::GridCanvas> canvas;

    LaneKeeper::View            view;
    ClipRefId                   selected = 0;
    std::shared_ptr<const Clip> shownClip;
    Tick                        lastPosition = -1;
    bool                        lastPlaying  = false;
    bool                        lastArmed    = false, lastRecording = false, lastSynthOpen = false;
    ClipRefId                   menuFor = 0;
    bool                        openMenu = false;
    // UMPIRE_PLUGIN_CAPTURE=DIR: a frame a second written there (frame-N.ppm),
    // for tools/PluginWindowCheck.mm, which can't screenshot without the
    // Screen Recording permission. Unset (always, in a host): nothing.
    std::string                 captureDir;
    int                         captured = 0;
    std::chrono::steady_clock::time_point nextCapture{};
};

void PluginEditor::Impl::build() {
    view = links.keeper->view();
    model.undo = [this] {
        links.keeper->undo();
        follow(true);
    };
    model.redo = [this] {
        links.keeper->redo();
        follow(true);
    };
    // An edit goes to the lane, which gives the audio thread what plays.
    model.editClip = [this](const std::string& label, const std::vector<NoteChange>& changes, const std::vector<NewNote>& added) {
        const LaneKeeper::Edited made = links.keeper->editTake(selected, label, changes, added);
        if (made.edit.clip) {
            shownClip    = made.edit.clip;
            model.origin = made.start - band.gridBar;
        }
        return made.edit;
    };

    band.view         = &view;
    band.selected     = &selected;
    band.select       = [this](ClipRefId ref) { choose(ref); };
    band.contextMenu  = [this](ClipRefId ref) { menuFor = ref, openMenu = true; };
    band.playheadSong = [this] { return links.engine->position(); };
    band.hostPlaying  = [this] { return links.engine->hostPlaying(); };

    {
        const dgl::Window::ScopedGraphicsContext context(window);
        canvas = std::make_unique<ui::GridCanvas>(window, model, probe);
    }
    canvas->setBand(&band);
    ui::GridPanels& panels      = canvas->panels();
    panels.hooks.fileMenu       = false;
    panels.hooks.nameEditable   = false;   // a take's name is its number (P4: names in the state)
    panels.hooks.menuBarStart   = [this] { record(); };
    panels.hooks.editMenuEnd    = [this] { takeItems(); };
    panels.hooks.menuBarEnd     = [this] {
        synthButton();
        status();
    };
    panels.hooks.frame          = [this] { takePopup(); };
    panels.hooks.inspectorShown = [this] { return selected != 0; };
    model.undoLabel = view.undoLabel;
    model.redoLabel = view.redoLabel;
    if (const char* dir = std::getenv("UMPIRE_PLUGIN_CAPTURE")) captureDir = dir;
    window.show();
}

void PluginEditor::Impl::choose(ClipRefId ref) {
    if (ref == selected) return;
    selected = ref;
    model.release();
    shownClip.reset();
    if (const ClipRef* t = selectedTake()) {
        showTake(*t);
        canvas->scrollHome();
    }
    canvas->panels().wake();
    canvas->repaint();
}

// The take in the grid, in song time from the bar it starts in.
void PluginEditor::Impl::showTake(const ClipRef& t) {
    if (shownClip == nullptr || t.start < band.gridBar) band.gridBar = barStart(t.start);
    model.origin = t.start - band.gridBar;
    shownClip    = t.clip;
    model.showClip(t.clip);
}

// The lane as the keeper has it now: a take recorded, an edit, an undo.
void PluginEditor::Impl::follow(bool force) {
    if (!force && links.keeper->version() == view.version) return;
    view = links.keeper->view();
    if (model.undoLabel != view.undoLabel || model.redoLabel != view.redoLabel) {
        model.undoLabel = view.undoLabel;
        model.redoLabel = view.redoLabel;
    }
    const ClipRef* t = selectedTake();
    if (selected != 0 && t == nullptr) {   // undone, deleted, or a state loaded
        selected = 0;
        model.release();
        shownClip.reset();
    } else if (t != nullptr && t->clip != shownClip && !canvas->dragging()) {
        showTake(*t);   // an undo or redo of this take (the grid's own edit already shows)
    }
    canvas->panels().wake();
    canvas->repaint();
}

void PluginEditor::Impl::record() {
    const bool  armed = links.armed && links.armed();
    const float h     = ImGui::GetFrameHeight() * 0.86f;
    ImGui::SetCursorPosY((ImGui::GetFrameHeight() - h) / 2 + 1);
    if (ui::toggle("##record", "Record", armed, col::kRecord, ImVec2(5.0f * ImGui::GetFontSize(), h)) && links.setArmed)
        links.setArmed(!armed);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(armed ? "Records while the host records. Click to stop recording here."
                                : "Off: the host's recording makes no take here. Click to record with the host.");
    ImGui::SameLine();
}

void PluginEditor::Impl::synthButton() {
    const std::string name = links.synthName ? links.synthName() : std::string();
    if (name.empty()) {
        ImGui::TextDisabled("No synth");
        return;
    }
    const bool  open = links.synthEditorOpen && links.synthEditorOpen();
    const float h    = ImGui::GetFrameHeight() * 0.86f;
    const float w    = ImGui::CalcTextSize(name.c_str()).x + 2.0f * ImGui::GetFontSize();
    ImGui::SetCursorPosY((ImGui::GetFrameHeight() - h) / 2 + 1);
    if (!links.toggleSynthEditor) {
        ImGui::TextDisabled("%s", name.c_str());
        return;
    }
    if (ui::toggle("##synth", name.c_str(), open, col::kNote, ImVec2(w, h))) links.toggleSynthEditor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(open ? "Close the synth's window" : "Open the synth's window");
}

// At the right: recording, what's listening, and where the host is.
void PluginEditor::Impl::status() {
    const std::string input = links.inputName ? links.inputName() : std::string();
    const std::string at    = position(links.engine->position());
    const bool        rec   = links.engine->recording();
    const float       right = ImGui::GetWindowWidth() - ImGui::GetStyle().ItemSpacing.x * 2;
    const float       posW  = ImGui::CalcTextSize("0000.0.0").x;
    const float       inW   = ImGui::CalcTextSize(input.empty() ? "No input" : input.c_str()).x;
    const float       recW  = rec ? ImGui::CalcTextSize("Recording").x + ImGui::GetFontSize() * 1.5f : 0;
    const float       x     = right - posW - inW - recW - ImGui::GetFontSize() * 2;
    if (x > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(x);
    if (rec) ui::badge("Recording", col::kRecord);
    if (input.empty()) ImGui::TextDisabled("No input");
    else ImGui::TextDisabled("%s", input.c_str());
    ImGui::SetCursorPosX(right - posW);
    ImGui::TextColored(links.engine->hostPlaying() ? col::kAccent : col::kSlate, "%s", at.c_str());
}

// The selected take's commands, in Edit and in its right-click menu.
void PluginEditor::Impl::takeItems() {
    const ClipRef* t      = selectedTake();
    const bool     newest = t != nullptr && !view.takes.empty() && view.takes.back().id == t->id;
    ImGui::Separator();
    if (ImGui::MenuItem("Bring take to front", nullptr, false, t != nullptr && !newest)) {
        links.keeper->bringToFront(selected);
        follow(true);
    }
    if (ImGui::MenuItem("Delete take", nullptr, false, t != nullptr)) {
        links.keeper->deleteTake(selected);
        follow(true);
    }
}

void PluginEditor::Impl::takePopup() {
    if (std::exchange(openMenu, false)) ImGui::OpenPopup("##take");
    if (!ImGui::BeginPopup("##take")) return;
    const ClipRef* t = selectedTake();
    if (t != nullptr && t->id == menuFor) {
        ImGui::TextDisabled("%s", t->clip->name.c_str());
        takeItems();
    } else {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

PluginEditor::PluginEditor(std::unique_ptr<Impl> i) : impl(std::move(i)) {}
PluginEditor::~PluginEditor() = default;

std::unique_ptr<PluginEditor> PluginEditor::open(std::uintptr_t parent, unsigned width, unsigned height, double scale,
                                                 EditorLinks links) {
    if (parent == 0 || links.keeper == nullptr || links.engine == nullptr) return nullptr;
    auto impl = std::make_unique<Impl>(parent, width, height, scale, std::move(links));
    impl->build();
    return std::unique_ptr<PluginEditor>(new PluginEditor(std::move(impl)));
}

void PluginEditor::idle() {
    Impl& m = *impl;
    m.follow();
    // The playhead and the header: redrawn when they change.
    const Tick at      = m.links.engine->position();
    const bool playing = m.links.engine->hostPlaying();
    const bool armed   = m.links.armed && m.links.armed();
    const bool rec     = m.links.engine->recording();
    const bool synth   = m.links.synthEditorOpen && m.links.synthEditorOpen();
    if (at != m.lastPosition || playing != m.lastPlaying || armed != m.lastArmed || rec != m.lastRecording ||
        synth != m.lastSynthOpen) {
        m.lastPosition = at, m.lastPlaying = playing, m.lastArmed = armed, m.lastRecording = rec, m.lastSynthOpen = synth;
        m.canvas->panels().wake(2);
        m.canvas->repaint();
    }
    if (!m.captureDir.empty() && std::chrono::steady_clock::now() >= m.nextCapture) {
        m.nextCapture = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        m.canvas->panels().captureNextFrame(m.captureDir + "/frame-" + std::to_string(++m.captured) + ".ppm");
        m.canvas->panels().wake(2);
        m.canvas->repaint();
    }
    m.app.idle();
}

void PluginEditor::setSize(unsigned width, unsigned height, double scale) {
    impl->scale = scale;
    impl->window.setSize(static_cast<uint>(std::lround(width * scale)), static_cast<uint>(std::lround(height * scale)));
}

#ifndef __APPLE__
double parentScale(std::uintptr_t) { return 1.0; }
#endif

} // namespace umpire::plugin
