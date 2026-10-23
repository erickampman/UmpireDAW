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

// A 16-bit mask as a choice reads: "All", "3" (one), "Several".
std::string maskLabel(std::uint16_t mask, const char* all) {
    if (mask == 0xFFFF) return all;
    int count = 0, only = 0;
    for (int i = 0; i < 16; ++i)
        if (mask & (1u << i)) ++count, only = i;
    if (count == 1) return std::to_string(only + 1);
    return count == 0 ? "None" : "Several";
}

// A choice of All or one of 1..16 (a mask of several shows, but is set elsewhere
// later). Returns true with the new mask when one is picked.
bool maskCombo(const char* id, const char* all, std::uint16_t mask, float width, std::uint16_t& picked) {
    ImGui::SetNextItemWidth(width);
    bool chose = false;
    if (ImGui::BeginCombo(id, maskLabel(mask, all).c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable(all, mask == 0xFFFF)) picked = 0xFFFF, chose = true;
        for (int i = 0; i < 16; ++i) {
            const std::uint16_t one = static_cast<std::uint16_t>(1u << i);
            if (ImGui::Selectable(std::to_string(i + 1).c_str(), mask == one)) picked = one, chose = true;
        }
        ImGui::EndCombo();
    }
    return chose;
}

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
    menuBar();
    headers();
    if (showMonitor) monitor();
    if (showDemo) ImGui::ShowDemoWindow(&showDemo);
}

void TrackPanels::menuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
#ifdef __APPLE__
    const char* quitKey = "Cmd+Q";
    const char* addKey  = "Cmd+T";
#else
    const char* quitKey = "Ctrl+Q";
    const char* addKey  = "Ctrl+T";
#endif
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Quit", quitKey)) quitRequested = true;
        ImGui::EndMenu();
    }
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float  w = ImGui::CalcTextSize("Track").x + ImGui::GetStyle().ItemSpacing.x * 2;
        trackMenuRect  = {p.x, 0, p.x + w, ImGui::GetFrameHeight()};
    }
    if (ImGui::BeginMenu("Track")) {
        if (ImGui::MenuItem("Add track", addKey)) controller.addTrack();
        if (ImGui::MenuItem("Remove track", nullptr, false, controller.selected() != 0))
            controller.removeTrack(controller.selected());
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("MIDI monitor", nullptr, &showMonitor);
        if (ImGui::MenuItem("Test grid window")) gridRequested = true;
        ImGui::Separator();
        ImGui::MenuItem("Diagnostics", nullptr, &showDiagnostics);
        ImGui::MenuItem("Dear ImGui demo window", nullptr, &showDemo);
        ImGui::EndMenu();
    }
    menuH = ImGui::GetWindowSize().y;
    ImGui::EndMainMenuBar();
}

// The headers: a window down the left, from the ruler to the monitor; each
// row placed where the canvas puts its lane.
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
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a track");

    // The rows, clipped below the ruler.
    const TrackViewModel& view = controller.view();
    const float lanesTop = canvas.lanesY() * sf;
    ImGui::PushClipRect(ImVec2(x, lanesTop), ImVec2(x + w, top + h), true);
    rowRects.clear();
    for (std::size_t i = 0; i < view.rows.size(); ++i) {
        const float y = canvas.rowY(static_cast<int>(i)) * sf;
        if (y + TrackCanvas::kRowH * sf < lanesTop || y > top + h) continue;
        row(view.rows[i], x, y, w);
    }
    if (view.rows.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(x + 0.8f * em, lanesTop + 0.8f * em));
        ImGui::TextDisabled("No tracks. Track > Add track.");
    }
    ImGui::PopClipRect();

    // The wheel over the headers scrolls the rows with the lanes.
    if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0) canvas.scrollBy(ImGui::GetIO().MouseWheel);
    ImGui::End();
}

void TrackPanels::row(const TrackRowModel& r, float x, float y, float w) {
    const float sf = static_cast<float>(getWindow().getScaleFactor());
    const float h  = TrackCanvas::kRowH * sf;
    const float em = ImGui::GetFontSize();
    const float pad = 0.5f * em;
    ImDrawList* d  = ImGui::GetWindowDrawList();
    ImGui::PushID(static_cast<int>(r.id));

    // The row: the selected one a step lighter, with the accent down its edge.
    const ImVec2 min(x, y), max(x + w, y + h);
    d->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(r.selected ? colour::kSection : colour::kPanel));
    if (r.selected) d->AddRectFilled(min, ImVec2(x + 3 * sf, y + h), ImGui::ColorConvertFloat4ToU32(colour::kAccent));
    d->AddLine(ImVec2(x, y + h - 0.5f), ImVec2(x + w, y + h - 0.5f), IM_COL32(40, 43, 51, 255));

    RowRects rects;
    rects.id  = r.id;
    rects.row = {min.x, min.y, max.x, max.y};

    const float line1 = y + pad * 0.8f;
    const float line2 = line1 + ImGui::GetFrameHeight() + pad * 0.5f;
    const float lightD = 0.9f * em;
    const float btn    = ImGui::GetFrameHeight();

    // Line 1: the light, the name, then R M S at the right.
    ImGui::SetCursorScreenPos(ImVec2(x + pad, line1 + (btn - lightD) / 2));
    light(r.midiActive, colour::kNote, lightD);
    ImGui::SetCursorScreenPos(ImVec2(x + pad + lightD + pad * 0.6f, line1));
    auto& buf = names[r.id];
    const float nameW = w - (pad + lightD + pad * 0.6f) - 3 * btn - 2 * (pad * 0.4f) - 2 * pad;
    ImGui::SetNextItemWidth(nameW);
    if (editing != r.id) std::snprintf(buf.data(), buf.size(), "%s", r.name.c_str());
    ImGui::InputText("##name", buf.data(), buf.size());
    if (ImGui::IsItemActivated()) {
        editing = r.id;
        controller.select(r.id);
    }
    if (ImGui::IsItemDeactivated()) {
        editing = 0;
        if (ImGui::IsItemDeactivatedAfterEdit()) controller.rename(r.id, buf.data());
    }
    rects.name = lastItem();

    float bx = x + w - pad - 3 * btn - 2 * (pad * 0.4f);
    ImGui::SetCursorScreenPos(ImVec2(bx, line1));
    if (toggle("##arm", "R", r.armed, colour::kRecord, ImVec2(btn, btn))) controller.setArmed(r.id, !r.armed);
    rects.arm = lastItem();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Arm to record");
    bx += btn + pad * 0.4f;
    ImGui::SetCursorScreenPos(ImVec2(bx, line1));
    if (toggle("##mute", "M", r.muted, colour::kAccent, ImVec2(btn, btn))) controller.setMuted(r.id, !r.muted);
    rects.mute = lastItem();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mute");
    bx += btn + pad * 0.4f;
    ImGui::SetCursorScreenPos(ImVec2(bx, line1));
    if (toggle("##solo", "S", r.soloed, colour::kNote, ImVec2(btn, btn))) controller.setSoloed(r.id, !r.soloed);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Solo");

    // Line 2: where its MIDI comes from (input, channel, group), and where it goes.
    const float inputW = 9.0f * em, maskW = 5.0f * em;
    ImGui::SetCursorScreenPos(ImVec2(x + pad, line2));
    ImGui::SetNextItemWidth(inputW);
    if (!r.inputConnected) ImGui::PushStyleColor(ImGuiCol_Text, colour::kRecord);
    const bool inputOpen = ImGui::BeginCombo("##input", r.inputLabel.c_str(), ImGuiComboFlags_HeightLarge);
    if (!r.inputConnected) ImGui::PopStyleColor();
    rects.input = lastItem();
    if (inputOpen) {
        const auto& choices = controller.view().inputs;
        for (std::size_t i = 0; i < choices.size(); ++i) {
            if (i == 2 || (i > 2 && choices[i].hardware != choices[i - 1].hardware) ||
                (choices[i].ref.kind == MidiSourceRef::Kind::Virtual &&
                 choices[i - 1].ref.kind != MidiSourceRef::Kind::Virtual))
                ImGui::Separator();
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(choices[i].label.c_str(), choices[i].ref == r.input)) controller.setInput(r.id, choices[i].ref);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Input");
    ImGui::SameLine(0, pad * 0.4f);
    std::uint16_t picked = 0;
    if (maskCombo("##channel", "All ch", r.channelMask, maskW, picked)) controller.setChannelMask(r.id, picked);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Channel");
    ImGui::SameLine(0, pad * 0.4f);
    if (maskCombo("##group", "All gr", r.groupMask, maskW, picked)) controller.setGroupMask(r.id, picked);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("UMP group");
    ImGui::SameLine(0, pad * 0.8f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("> %s", r.outputLabel.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Output: a MIDI device (milestone 7) or a plugin (milestone 8)");

    // A click on the row's background selects the track.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(min, max) && !ImGui::IsAnyItemHovered())
        controller.select(r.id);

    rowRects.push_back(rects);
    ImGui::PopID();
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
        ImGui::TextDisabled("Play something: every input Umpire has open shows here, with the tracks that take it.");
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
