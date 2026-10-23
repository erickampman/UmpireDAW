#include "Ui/GridPanels.h"

#include <cmath>
#include <cstdio>
#include <string>

#include "Window.hpp"

#include "imgui.h"
#include "Ui/Widgets.h"

namespace umpire::ui {

namespace {

GridPanels::Rect lastItem() {
    return {ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y, ImGui::GetItemRectMax().x,
            ImGui::GetItemRectMax().y};
}

} // namespace

GridPanels::GridPanels(DGL_NAMESPACE::Widget* parent, GridModel& m, Probe& p) : ImGuiWidget(parent), model(m), probe(p) {
    rescale();   // onStyle() is ours only now that the object is whole
}

void GridPanels::onStyle(ImGuiStyle& style) {
    applyTheme(style);
}

void GridPanels::routed(Input input, bool toImGui) {
    switch (input) {
    case Input::Press: ++(toImGui ? probe.imguiPresses : probe.canvasPresses); break;
    case Input::Key: ++(toImGui ? probe.imguiKeys : probe.canvasKeys); break;
    case Input::Scroll: ++(toImGui ? probe.imguiScrolls : probe.canvasScrolls); break;
    }
}

void GridPanels::onImGuiDisplay() {
    menuBar();
    const float sf = static_cast<float>(getWindow().getScaleFactor());
    panelW         = std::round(280.0f * sf);
    inspector(static_cast<float>(getWidth()), static_cast<float>(getHeight()));
    if (showDemo) ImGui::ShowDemoWindow(&showDemo);
}

void GridPanels::menuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
#ifdef __APPLE__
    const char* quitKey = "Cmd+Q";
#else
    const char* quitKey = "Ctrl+Q";
#endif
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New clip")) model.newClip();
        ImGui::Separator();
        if (ImGui::MenuItem("Close window")) closeRequested = true;
        if (ImGui::MenuItem("Quit", quitKey)) quitRequested = true;
        ImGui::EndMenu();
    }
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float  w = ImGui::CalcTextSize("Edit").x + ImGui::GetStyle().ItemSpacing.x * 2;
        editMenuRect   = {p.x, 0, p.x + w, ImGui::GetFrameHeight()};
    }
    editMenuOpen = ImGui::BeginMenu("Edit");
    if (editMenuOpen) {
        if (ImGui::MenuItem("Delete note", "Del", false, model.selectedNote() != nullptr)) model.deleteSelected();
        if (ImGui::MenuItem("Clear all")) model.clear();
        clearAllItemRect = lastItem();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Diagnostics", nullptr, &showDiagnostics);
        ImGui::MenuItem("Dear ImGui demo window", nullptr, &showDemo);
        ImGui::EndMenu();
    }
    menuH = ImGui::GetWindowSize().y;
    ImGui::EndMainMenuBar();
}

void GridPanels::inspector(float W, float H) {
    ImGui::SetNextWindowPos(ImVec2(W - panelW, menuH));
    ImGui::SetNextWindowSize(ImVec2(panelW, H - menuH));
    ImGui::Begin("Inspector", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);

    beginSection("Clip", colour::kAccent, false);
    if (!textActive()) std::snprintf(nameBuf, sizeof nameBuf, "%s", model.clipName.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("##name", nameBuf, sizeof nameBuf)) {
        model.clipName = nameBuf;
        model.touch();
    }
    nameFieldRect = lastItem();
    endSection();

    beginSection("Note", colour::kNote, false);
    if (GridNote* n = model.selectedNote()) {
        // Velocity is a knob, the plugins' way: caption above, value below.
        const float em   = ImGui::GetFontSize();
        const float knobD = 3.2f * em;
        const float col   = ImGui::GetContentRegionAvail().x;
        const float x     = ImGui::GetCursorPosX();
        caption("Velocity", col, colour::kNote);
        ImGui::SetCursorPosX(x + (col - knobD) / 2);
        float v = static_cast<float>(n->velocity - GridModel::kMinVelocity) /
                  (GridModel::kMaxVelocity - GridModel::kMinVelocity);
        if (knob("##velocity", v, 99.0f / 126, knobD, colour::kNote))
            model.setSelectedVelocity(GridModel::kMinVelocity +
                                      static_cast<int>(std::lround(v * (GridModel::kMaxVelocity - GridModel::kMinVelocity))));
        velocityRect = lastItem();
        ImGui::SetCursorPosX(x);
        valueBox("##velocityValue", std::to_string(model.selectedNote()->velocity), col);

        ImGui::PushItemWidth(-4.5f * em);
        int pitch = n->pitch, start = n->start, length = n->length;
        if (sliderRow("Pitch", pitch, 0, GridModel::kPitches - 1, colour::kNote)) model.moveSelected(n->start, pitch);
        if (sliderRow("Start", start, 0, model.steps - n->length, colour::kNote)) model.moveSelected(start, n->pitch);
        if (sliderRow("Length", length, 1, model.steps - n->start, colour::kNote)) model.setSelectedLength(length);
        ImGui::PopItemWidth();
        if (ImGui::Button("Delete note")) model.deleteSelected();
    } else {
        ImGui::TextDisabled("No note selected.");
        ImGui::TextDisabled("Click the grid to add one.");
        velocityRect = {};
    }
    endSection();

    if (showDiagnostics) {
        if (beginSection("Diagnostics", colour::kSlate, true)) {
            ImGui::Text("Frames drawn: %llu", static_cast<unsigned long long>(framesDrawn()));
            ImGui::Text("Frames, last second: %d", probe.framesLastSecond);
            ImGui::Text("Scale factor: %.2f", getWindow().getScaleFactor());
            ImGui::Text("Presses: canvas %d, ImGui %d", probe.canvasPresses, probe.imguiPresses);
            ImGui::Text("Keys: canvas %d, ImGui %d", probe.canvasKeys, probe.imguiKeys);
            ImGui::Text("Scrolls: canvas %d, ImGui %d", probe.canvasScrolls, probe.imguiScrolls);
            ImGui::Text("ImGui %s", IMGUI_VERSION);
        }
        endSection();
    }
    ImGui::End();
}

} // namespace umpire::ui
