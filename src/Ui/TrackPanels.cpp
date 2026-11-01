#include "Ui/TrackPanels.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "Window.hpp"

#include "imgui.h"
#include "Ui/TrackCanvas.h"
#include "Ui/Widgets.h"

namespace umpire::ui {

namespace {

TrackPanels::Rect lastItem() {
    return {ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y, ImGui::GetItemRectMax().x,
            ImGui::GetItemRectMax().y};
}

#ifdef __APPLE__
constexpr const char* kNewKey    = "Cmd+N";
constexpr const char* kOpenKey   = "Cmd+O";
constexpr const char* kSaveKey   = "Cmd+S";
constexpr const char* kSaveAsKey = "Cmd+Shift+S";
constexpr const char* kQuitKey = "Cmd+Q";
constexpr const char* kAddKey  = "Cmd+T";
constexpr const char* kUndoKey = "Cmd+Z";
constexpr const char* kRedoKey = "Cmd+Shift+Z";
constexpr const char* kRecKey  = "Cmd+R";
#else
constexpr const char* kNewKey    = "Ctrl+N";
constexpr const char* kOpenKey   = "Ctrl+O";
constexpr const char* kSaveKey   = "Ctrl+S";
constexpr const char* kSaveAsKey = "Ctrl+Shift+S";
constexpr const char* kQuitKey = "Ctrl+Q";
constexpr const char* kAddKey  = "Ctrl+T";
constexpr const char* kUndoKey = "Ctrl+Z";
constexpr const char* kRedoKey = "Ctrl+Shift+Z";
constexpr const char* kRecKey  = "Ctrl+R";
#endif

} // namespace

TrackPanels::TrackPanels(TrackCanvas* parent, TrackController& c, Probe& p)
    : ImGuiWidget(parent), canvas(*parent), controller(c), probe(p) {
    rescale();   // onStyle() is ours only now that the object is whole
}

void TrackPanels::onStyle(ImGuiStyle& style) {
    applyTheme(style);
}

void TrackPanels::routed(Input input, bool toImGui) {
    switch (input) {
    case Input::Press: ++(toImGui ? probe.imguiPresses : probe.canvasPresses); break;
    case Input::Key: ++(toImGui ? probe.imguiKeys : probe.canvasKeys); break;
    case Input::Scroll: ++(toImGui ? probe.imguiScrolls : probe.canvasScrolls); break;
    }
}

void TrackPanels::onImGuiDisplay() {
    const float sf = static_cast<float>(getWindow().getScaleFactor());
    monitorH       = std::round(170.0f * sf);
    statusH        = std::round(ImGui::GetFrameHeight() + 4 * sf);
    menuBar();
    headers();
    status();
    if (showMonitor) monitor();
    if (showDemo) ImGui::ShowDemoWindow(&showDemo);
    promptPopup();
}

// MainWindow's question, as a modal: Enter takes the first button, Escape the
// last when there's a choice.
void TrackPanels::promptPopup() {
    constexpr const char* kId = "##prompt";
    if (!prompt) return;
    if (!ImGui::IsPopupOpen(kId)) ImGui::OpenPopup(kId);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kId, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                                  ImGuiWindowFlags_NoMove)) return;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
    ImGui::TextUnformatted(prompt->text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    const std::size_t count  = prompt->buttons.size();
    int               chosen = -1;
    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::Button(prompt->buttons[i].c_str(), ImVec2(ImGui::GetFontSize() * 6, 0))) chosen = static_cast<int>(i);
    }
    if (chosen < 0 && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) chosen = 0;
    if (chosen < 0 && ImGui::IsKeyPressed(ImGuiKey_Escape)) chosen = static_cast<int>(count) - 1;
    if (chosen >= 0) {
        promptAnswer = chosen;
        prompt.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void TrackPanels::menuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    const auto menuRect = [](const char* title) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float  w = ImGui::CalcTextSize(title).x + ImGui::GetStyle().ItemSpacing.x * 2;
        return Rect{p.x, 0, p.x + w, ImGui::GetFrameHeight()};
    };
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New", kNewKey)) fileRequested = FileAction::New;
        if (ImGui::MenuItem("Open...", kOpenKey)) fileRequested = FileAction::Open;
        if (ImGui::MenuItem("Save", kSaveKey)) fileRequested = FileAction::Save;
        if (ImGui::MenuItem("Save As...", kSaveAsKey)) fileRequested = FileAction::SaveAs;
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", kQuitKey)) fileRequested = FileAction::Quit;
        ImGui::EndMenu();
    }
    const TrackViewModel& view = controller.view();
    editMenuRect = menuRect("Edit");
    if (ImGui::BeginMenu("Edit")) {
        const std::string undo = view.undoLabel.empty() ? "Undo" : "Undo " + view.undoLabel;
        const std::string redo = view.redoLabel.empty() ? "Redo" : "Redo " + view.redoLabel;
        if (ImGui::MenuItem(undo.c_str(), kUndoKey, false, !view.undoLabel.empty())) controller.undo();
        if (ImGui::MenuItem(redo.c_str(), kRedoKey, false, !view.redoLabel.empty())) controller.redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Delete clip", "Del", false, controller.selectedClip() != 0))
            controller.deleteClip(controller.selectedClip());
        ImGui::EndMenu();
    }
    trackMenuRect = menuRect("Track");
    if (ImGui::BeginMenu("Track")) {
        if (ImGui::MenuItem("Add track", kAddKey)) controller.addTrack();
        if (ImGui::MenuItem("Remove track", nullptr, false, controller.selected() != 0))
            controller.removeTrack(controller.selected());
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Transport")) {
        const TransportModel& tr = view.transport;
        if (ImGui::MenuItem(tr.playing ? "Stop" : "Play", "Space")) tr.playing ? controller.stop() : controller.play();
        if (ImGui::MenuItem(tr.recording ? "Stop recording" : "Record", kRecKey)) controller.record();
        if (ImGui::MenuItem("Go to start", "Home")) controller.locate(0);
        ImGui::Separator();
        if (ImGui::MenuItem("Loop", "L", tr.looping)) controller.setLooping(!tr.looping);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("MIDI monitor", nullptr, &showMonitor);
        ImGui::Separator();
        ImGui::MenuItem("Diagnostics", nullptr, &showDiagnostics);
        ImGui::MenuItem("Dear ImGui demo window", nullptr, &showDemo);
        ImGui::EndMenu();
    }
    transport();
    menuH = ImGui::GetWindowSize().y;
    ImGui::EndMainMenuBar();
}

// The transport, in the menu bar from the lanes' left edge: to the start,
// stop, play, record, loop, the position and the tempo.
void TrackPanels::transport() {
    const float sf  = static_cast<float>(getWindow().getScaleFactor());
    const float em  = ImGui::GetFontSize();
    const float btn = ImGui::GetFrameHeight();
    const float gap = 0.3f * em;
    const TransportModel& tr = controller.view().transport;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + em, TrackCanvas::kHeaderW * sf));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 0));
    if (transportButton("##start", Glyph::ToStart, false, colour::kSlate, ImVec2(btn, btn))) controller.locate(0);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go to start (Home)");
    ImGui::SameLine();
    if (transportButton("##stop", Glyph::Stop, false, colour::kSlate, ImVec2(btn, btn))) controller.stop();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop (Space)");
    ImGui::SameLine();
    if (transportButton("##play", Glyph::Play, tr.playing, colour::kNote, ImVec2(btn, btn)))
        tr.playing ? controller.stop() : controller.play();
    playRect = lastItem();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play (Space)");
    ImGui::SameLine();
    if (transportButton("##record", Glyph::Record, tr.recording, colour::kRecord, ImVec2(btn, btn))) controller.record();
    recordRect = lastItem();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Record on the armed tracks (%s)", kRecKey);
    ImGui::SameLine();
    if (toggle("##loop", "Loop", tr.looping, colour::kAccent, ImVec2(ImGui::CalcTextSize("Loop").x + btn * 0.6f, btn)))
        controller.setLooping(!tr.looping);
    loopRect = lastItem();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Loop (L); drag along the ruler to set where");

    ImGui::SameLine(0, em);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(tr.playing ? colour::kAccent : ImGui::GetStyle().Colors[ImGuiCol_Text], "%s",
                       tr.positionLabel.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bar, beat, sixteenth");
    ImGui::SameLine(0, 0.6f * em);
    ImGui::TextDisabled("%s", tr.timeLabel.c_str());

    // The tempo: dragged as one change, kept when let go.
    ImGui::SameLine(0, em);
    if (!tempoActive) tempoEdit = static_cast<float>(tr.tempoBpm);
    ImGui::SetNextItemWidth(4.5f * em);
    ImGui::DragFloat("##tempo", &tempoEdit, 0.2f, static_cast<float>(TrackController::kMinTempo),
                     static_cast<float>(TrackController::kMaxTempo), "%.1f", ImGuiSliderFlags_AlwaysClamp);
    tempoActive = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit()) controller.setTempo(tempoEdit);
    tempoRect = lastItem();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tempo: drag, or Ctrl+click to type");
    ImGui::SameLine(0, 0.3f * em);
    ImGui::TextDisabled("bpm");
    ImGui::PopStyleVar();
}

// The headers: a window down the left, from the ruler to the status line;
// each row placed where the canvas puts its lane.
void TrackPanels::headers() {
    const float sf = static_cast<float>(getWindow().getScaleFactor());
    const float x = 0, w = std::round(TrackCanvas::kHeaderW * sf);
    const float top = std::round(canvas.rulerY() * sf), h = std::round((canvas.lanesY() + canvas.lanesH()) * sf) - top;
    ImGui::SetNextWindowPos(ImVec2(x, top));
    ImGui::SetNextWindowSize(ImVec2(w, std::max(1.0f, h)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("Tracks", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar(2);

    // The corner, beside the ruler: "Tracks" and a + to add one.
    const float em = ImGui::GetFontSize();
    const float rulerH = TrackCanvas::kRulerH * sf;
    ImGui::SetCursorScreenPos(ImVec2(x + 0.6f * em, top + (rulerH - ImGui::GetTextLineHeight()) / 2));
    ImGui::TextColored(colour::kSlate, "Tracks");
    const float plus = rulerH - 6 * sf;
    ImGui::SetCursorScreenPos(ImVec2(x + w - plus - 0.5f * em, top + 3 * sf));
    if (toggle("##add", "+", false, colour::kAccent, ImVec2(plus, plus))) controller.addTrack();
    addTrackRect = lastItem();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a track (%s)", kAddKey);

    // The rows, clipped below the ruler. An empty session has none: the
    // canvas shows the prompt.
    const TrackViewModel& view = controller.view();
    const float lanesTop = canvas.lanesY() * sf;
    ImGui::PushClipRect(ImVec2(x, lanesTop), ImVec2(x + w, top + h), true);
    rowRects.clear();
    for (std::size_t i = 0; i < view.rows.size(); ++i) {
        const float y = canvas.rowY(static_cast<int>(i)) * sf;
        if (y + TrackCanvas::kRowH * sf < lanesTop || y > top + h) continue;
        row(view.rows[i], x, y, w);
    }
    ImGui::PopClipRect();

    // The wheel over the headers scrolls the rows with the lanes.
    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0) canvas.scrollBy(ImGui::GetIO().MouseWheel);
    ImGui::End();
}

// A row: the light and the name, then badges for what isn't the default
// (armed, muted, soloed). The touched row shows its controls in place instead:
// the name to type, R M S, and where its MIDI goes.
void TrackPanels::row(const TrackRowModel& r, float x, float y, float w) {
    const float sf = static_cast<float>(getWindow().getScaleFactor());
    const float h  = TrackCanvas::kRowH * sf;
    const float em = ImGui::GetFontSize();
    const float pad = 0.5f * em;
    ImDrawList* d  = ImGui::GetWindowDrawList();
    ImGui::PushID(static_cast<int>(r.id));

    // The row: the touched one a step lighter, with the accent down its edge.
    const ImVec2 min(x, y), max(x + w, y + h);
    d->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(r.touched ? colour::kSection : colour::kPanel));
    if (r.touched) d->AddRectFilled(min, ImVec2(x + 3 * sf, y + h), ImGui::ColorConvertFloat4ToU32(colour::kAccent));
    d->AddLine(ImVec2(x, y + h - 0.5f), ImVec2(x + w, y + h - 0.5f), IM_COL32(40, 43, 51, 255));

    RowRects rects;
    rects.id  = r.id;
    rects.row = {min.x, min.y, max.x, max.y};

    const float line1  = y + pad * 0.8f;
    const float line2  = line1 + ImGui::GetFrameHeight() + pad * 0.5f;
    const float lightD = 0.9f * em;
    const float btn    = ImGui::GetFrameHeight();
    const float nameX  = x + pad + lightD + pad * 0.6f;

    ImGui::SetCursorScreenPos(ImVec2(x + pad, line1 + (btn - lightD) / 2));
    light(r.midiActive, colour::kNote, lightD);

    if (r.touched) {
        // The name to type, then R M S at the right.
        ImGui::SetCursorScreenPos(ImVec2(nameX, line1));
        auto& buf = names[r.id];
        ImGui::SetNextItemWidth(x + w - pad - 3 * btn - 2 * (pad * 0.4f) - pad - nameX);
        if (editing != r.id) std::snprintf(buf.data(), buf.size(), "%s", r.name.c_str());
        ImGui::InputText("##name", buf.data(), buf.size());
        if (ImGui::IsItemActivated()) editing = r.id;
        if (ImGui::IsItemDeactivated()) {
            editing = 0;
            if (ImGui::IsItemDeactivatedAfterEdit()) controller.rename(r.id, buf.data());
        }
        rects.name = lastItem();

        float bx = x + w - pad - 3 * btn - 2 * (pad * 0.4f);
        ImGui::SetCursorScreenPos(ImVec2(bx, line1));
        if (toggle("##arm", "R", r.armed, colour::kRecord, ImVec2(btn, btn))) controller.setArmed(r.id, !r.armed);
        rects.arm = lastItem();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Arm to record (R)");
        bx += btn + pad * 0.4f;
        ImGui::SetCursorScreenPos(ImVec2(bx, line1));
        if (toggle("##mute", "M", r.muted, colour::kAccent, ImVec2(btn, btn))) controller.setMuted(r.id, !r.muted);
        rects.mute = lastItem();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mute");
        bx += btn + pad * 0.4f;
        ImGui::SetCursorScreenPos(ImVec2(bx, line1));
        if (toggle("##solo", "S", r.soloed, colour::kNote, ImVec2(btn, btn))) controller.setSoloed(r.id, !r.soloed);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Solo");

        // Where its MIDI goes: a device, or a CLAP instrument (milestone 8),
        // with an Edit button for the instrument's editor (milestone 11).
        ImGui::SetCursorScreenPos(ImVec2(nameX, line2));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled(">");
        ImGui::SameLine(0, pad * 0.5f);
        const float comboRight = x + w - pad - (r.outputIsPlugin ? btn + pad * 0.4f : 0.0f);
        ImGui::SetNextItemWidth(comboRight - ImGui::GetCursorScreenPos().x);
        const std::string shown = r.outputMissing ? r.outputLabel + " (not connected)" : r.outputLabel;
        if (r.outputMissing) ImGui::PushStyleColor(ImGuiCol_Text, colour::kRecord);
        const bool open = ImGui::BeginCombo("##output", shown.c_str());
        if (r.outputMissing) ImGui::PopStyleColor();
        outputRect = lastItem();
        if (open) {
            const std::string* now = controller.session().outputOf(r.id);
            if (ImGui::Selectable("No output", now == nullptr)) controller.setOutput(r.id, "");
            const std::vector<OutputChoice>& choices = controller.view().outputs;
            if (!choices.empty()) ImGui::Separator();
            for (const OutputChoice& c : choices) {
                ImGui::PushID(c.device.c_str());
                if (ImGui::Selectable(c.label.c_str(), now != nullptr && *now == c.device)) controller.setOutput(r.id, c.device);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", c.device.c_str());
                ImGui::PopID();
            }
            if (choices.empty()) ImGui::TextDisabled("Nothing to play to");
            ImGui::EndCombo();
        } else if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Where the track plays: a MIDI device or a CLAP instrument");
        }
        if (r.outputIsPlugin) {
            ImGui::SameLine(0, pad * 0.4f);
            if (toggle("##edit", "E", r.editorOpen, colour::kAccent, ImVec2(btn, btn))) {
                if (r.editorOpen) controller.closeEditor(r.id);
                else controller.openEditor(r.id);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(r.editorOpen ? "Close the plugin's editor" : "Open the plugin's editor");
        }
    } else {
        // The name, and badges only for what isn't the default.
        ImGui::SetCursorScreenPos(ImVec2(nameX, line1));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(r.name.c_str());
        rects.name = lastItem();
        float bx = x + w - pad;
        const auto right = [&](const char* text, const ImVec4& c) {
            const float bw = ImGui::CalcTextSize(text).x + ImGui::GetTextLineHeight() * 0.7f;
            bx -= bw;
            ImGui::SetCursorScreenPos(ImVec2(bx, line1));
            badge(text, c);
            bx -= pad * 0.4f;
        };
        if (r.soloed) right("S", colour::kNote);
        if (r.muted) right("M", colour::kAccent);
        if (r.armed) right("R", colour::kRecord);
        // Where it plays, once it plays somewhere: what it sounds through.
        if (controller.session().outputOf(r.id) != nullptr) {
            ImGui::SetCursorScreenPos(ImVec2(nameX, line2));
            ImGui::AlignTextToFramePadding();
            if (r.outputMissing) ImGui::TextColored(colour::kRecord, "> %s (not connected)", r.outputLabel.c_str());
            else ImGui::TextDisabled("> %s", r.outputLabel.c_str());
        }
    }

    // A click on the row, but not on a control, touches the track.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(min, max) && !ImGui::IsAnyItemHovered())
        controller.select(r.id);

    rowRects.push_back(rects);
    ImGui::PopID();
}

// The status line, above the monitor: what's listening and where playing
// goes (or that recording has nowhere to go).
void TrackPanels::status() {
    const float W = static_cast<float>(getWidth()), H = static_cast<float>(getHeight());
    const float y = H - monitorHeight() - statusH;
    ImGui::SetNextWindowPos(ImVec2(0, y));
    ImGui::SetNextWindowSize(ImVec2(W, statusH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize() * 0.6f, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, colour::kPanel);
    ImGui::Begin("Status", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    statusRect = {0, y, W, y + statusH};
    const TrackViewModel& view = controller.view();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(colour::kSlate, "Listening:");
    ImGui::SameLine();
    ImGui::TextUnformatted(view.listening.c_str());
    ImGui::SameLine(0, ImGui::GetFontSize());
    if (view.warning) ImGui::TextColored(colour::kRecord, "%s", view.status.c_str());
    else ImGui::TextColored(colour::kNote, "%s", view.status.c_str());
    ImGui::End();
}

void TrackPanels::monitor() {
    const float W = static_cast<float>(getWidth()), H = static_cast<float>(getHeight());
    ImGui::SetNextWindowPos(ImVec2(0, H - monitorH));
    ImGui::SetNextWindowSize(ImVec2(W, monitorH));
    ImGui::Begin("MIDI monitor", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
    monitorRect = {0, H - monitorH, W, H};
    const TrackViewModel& view = controller.view();
    ImGui::TextColored(colour::kNote, "MIDI monitor");
    ImGui::SameLine();
    ImGui::TextDisabled("%s, %llu received%s", view.midiBackend.c_str(),
                        static_cast<unsigned long long>(view.monitorCount),
                        view.midiDropped > 0 ? (", " + std::to_string(view.midiDropped) + " dropped").c_str() : "");
    ImGui::SameLine(ImGui::GetWindowWidth() - 4.5f * ImGui::GetFontSize());
    if (ImGui::SmallButton("Clear")) controller.clearMonitor();
    if (showDiagnostics) {
        ImGui::SameLine(ImGui::GetWindowWidth() * 0.45f);
        ImGui::TextDisabled("frames %llu (%d/s), presses %d/%d, keys %d/%d, ImGui %s",
                            static_cast<unsigned long long>(framesDrawn()), probe.framesLastSecond, probe.canvasPresses,
                            probe.imguiPresses, probe.canvasKeys, probe.imguiKeys, IMGUI_VERSION);
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, colour::kWell);
    ImGui::BeginChild("##lines", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (view.monitor.empty()) {
        ImGui::TextDisabled("Play something: every input Umpire listens to shows here, with the tracks it reaches.");
        ImGui::TextDisabled("Other apps (KeyEDO) can send to Umpire's virtual inputs, \"MIDI 2.0 In\" and \"MIDI 1.0 In\".");
    }
    if (ImGui::BeginTable("##table", 5, ImGuiTableFlags_SizingFixedFit)) {
        const float em = ImGui::GetFontSize();
        ImGui::TableSetupColumn("time", ImGuiTableColumnFlags_WidthFixed, 5 * em);
        ImGui::TableSetupColumn("source", ImGuiTableColumnFlags_WidthFixed, 9 * em);
        ImGui::TableSetupColumn("words", ImGuiTableColumnFlags_WidthFixed, 17 * em);
        ImGui::TableSetupColumn("text", ImGuiTableColumnFlags_WidthFixed, 22 * em);
        ImGui::TableSetupColumn("tracks", ImGuiTableColumnFlags_WidthStretch);
        for (const MonitorLine& line : view.monitor) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%9.3f", line.seconds);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(line.source.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", line.words.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(line.text.c_str());
            ImGui::TableNextColumn();
            if (line.tracks.empty()) ImGui::TextDisabled("(no track)");
            else ImGui::TextColored(colour::kNote, "> %s", line.tracks.c_str());
        }
        ImGui::EndTable();
    }
    // Follow the newest line, unless scrolled back to read.
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
}

} // namespace umpire::ui
