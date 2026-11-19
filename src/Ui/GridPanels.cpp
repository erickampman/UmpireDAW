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
    const float sf    = static_cast<float>(getWindow().getScaleFactor());
    const bool  shown = !hooks.inspectorShown || hooks.inspectorShown();
    panelW            = shown ? std::round(280.0f * sf) : 0;
    if (shown) inspector(static_cast<float>(getWidth()), static_cast<float>(getHeight()));
    if (hooks.frame) hooks.frame();
    if (showDemo) ImGui::ShowDemoWindow(&showDemo);
}

void GridPanels::menuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
#ifdef __APPLE__
    const char* quitKey = "Cmd+Q";
#else
    const char* quitKey = "Ctrl+Q";
#endif
    if (hooks.fileMenu && ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Close window")) closeRequested = true;
        if (ImGui::MenuItem("Quit", quitKey)) quitRequested = true;
        ImGui::EndMenu();
    }
    if (hooks.menuBarStart) hooks.menuBarStart();
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float  w = ImGui::CalcTextSize("Edit").x + ImGui::GetStyle().ItemSpacing.x * 2;
        editMenuRect   = {p.x, 0, p.x + w, ImGui::GetFrameHeight()};
    }
    editMenuOpen = ImGui::BeginMenu("Edit");
    if (editMenuOpen) {
        // The session's undo, as the track view's Edit menu.
#ifdef __APPLE__
        const char* undoKey = "Cmd+Z";
        const char* redoKey = "Cmd+Shift+Z";
#else
        const char* undoKey = "Ctrl+Z";
        const char* redoKey = "Ctrl+Shift+Z";
#endif
        const std::string undo = model.undoLabel.empty() ? "Undo" : "Undo " + model.undoLabel;
        const std::string redo = model.redoLabel.empty() ? "Redo" : "Redo " + model.redoLabel;
        if (ImGui::MenuItem(undo.c_str(), undoKey, false, model.undo && !model.undoLabel.empty())) model.undo();
        if (ImGui::MenuItem(redo.c_str(), redoKey, false, model.redo && !model.redoLabel.empty())) model.redo();
        ImGui::Separator();
        const int selected = model.selectionSize();
        if (ImGui::MenuItem("Quantize notes", "Q", false, selected > 0)) {
            model.quantizeSelected();
            model.commit("Quantize notes");
        }
        if (ImGui::MenuItem(selected > 1 ? "Delete notes" : "Delete note", "Del", false, selected > 0)) {
            model.deleteSelected();
            model.commit();
        }
        if (ImGui::MenuItem("Delete all notes", nullptr, false, !model.notes.empty())) {
            model.clear();
            model.commit();
        }
        clearAllItemRect = lastItem();
        if (hooks.editMenuEnd) hooks.editMenuEnd();
        ImGui::EndMenu();
    }
    if (hooks.fileMenu && ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Diagnostics", nullptr, &showDiagnostics);
        ImGui::MenuItem("Dear ImGui demo window", nullptr, &showDemo);
        ImGui::EndMenu();
    }
    if (hooks.menuBarEnd) hooks.menuBarEnd();
    menuH = ImGui::GetWindowSize().y;
    ImGui::EndMainMenuBar();
}

// What the note is: its row's step and pitch, and for a note from the clip,
// what was sent (the rows and the 7-bit velocity are only the grid's view).
void GridPanels::notePitch(const GridNote& n) {
    const auto hz = [](double semitones) { return 440.0 * std::pow(2.0, (semitones - 69) / 12); };
    const double row = model.rowSemitones(n.pitch);
    ImGui::Spacing();
    ImGui::TextDisabled("Step %d of %d-EDO from %s", n.pitch - model.referenceRow(), model.edo,
                        pitchName(model.referenceNote).c_str());
    if (!n.fromClip) {
        ImGui::TextDisabled("%.3f semitones: %s, %.1f Hz", row, pitchName(row).c_str(), hz(row));
        return;
    }
    const double sent = n.pitch79 / 512.0;
    switch (n.pitchFrom) {
    case PitchFrom::Attribute:
        ImGui::Text("Pitch 7.9 attribute on note %d", n.sentNote);
        ImGui::TextDisabled("7.9 value 0x%04X", n.pitch79);
        break;
    case PitchFrom::Pitch725:
        ImGui::Text("Pitch 7.25 for key %d", n.sentNote);
        ImGui::TextDisabled("as 7.9: 0x%04X", n.pitch79);
        break;
    case PitchFrom::NoteNumber: ImGui::Text("Note %d, no pitch sent (12-EDO)", n.sentNote); break;
    }
    ImGui::Text("%.3f semitones: %s", sent, pitchName(sent).c_str());
    ImGui::TextDisabled("%.2f Hz", hz(sent));
    if (const double off = (row - sent) * 100; n.pitch != n.clipRow)
        ImGui::TextColored(colour::kAccent, "Moved: the row is %+.0f cents from it", off);
    else if (std::abs(off) >= 1)
        ImGui::TextColored(colour::kAccent, "%+.0f cents from its row", -off);
    ImGui::TextDisabled("Velocity 0x%04X of 0xFFFF (16-bit)", n.velocity16);
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
    // The clip's name, a command when Enter or a click elsewhere ends the edit.
    ImGui::InputText("##name", nameBuf, sizeof nameBuf, hooks.nameEditable ? 0 : ImGuiInputTextFlags_ReadOnly);
    if (ImGui::IsItemDeactivatedAfterEdit()) model.setClipName(nameBuf);
    nameFieldRect = lastItem();
    endSection();

    // The tuning, as StageHand's sequence editor: the EDO and the note its
    // step 0 sounds: the clip's scale (decisions.md, "Time, clips and pitch").
    beginSection("Tuning", colour::kNote, false);
    {
        const float em = ImGui::GetFontSize();
        ImGui::PushItemWidth(-4.5f * em);
        int edo = model.edo, reference = model.referenceNote;
        bool moved = sliderRow("EDO", edo, Scale::kMinEdo, Scale::kMaxEdo, colour::kNote);
        moved |= sliderRow("Step 0 at", reference, 0, 127, colour::kNote);
        ImGui::PopItemWidth();
        // A drag shows here as it goes; letting go makes it the clip's, once.
        if (moved) {
            model.setTuning(edo, reference);
            tuningPending = model.chooseScale != nullptr;
        }
        if (tuningPending && !ImGui::IsAnyItemActive()) {
            tuningPending = false;
            model.chooseScale(model.edo, model.referenceNote);
        }
        switch (model.scaleFit) {
        case ScaleFit::WorkedOut: ImGui::TextDisabled("Worked out from the notes"); break;
        case ScaleFit::Chosen: ImGui::TextDisabled("Set by hand"); break;
        case ScaleFit::NoFit: ImGui::TextColored(colour::kAccent, "No EDO fits: notes on the nearest rows"); break;
        case ScaleFit::Unsure: {
            std::string fits;
            for (int c : model.scaleCandidates) fits += (fits.empty() ? "" : ", ") + std::to_string(c);
            ImGui::TextColored(colour::kAccent, "Not sure: %s fit", fits.c_str());
            ImGui::TextDisabled("Too few notes to tell. Set the EDO.");
            break;
        }
        case ScaleFit::Default: break;
        }
    }
    endSection();

    beginSection("Note", colour::kNote, false);
    if (GridNote* n = model.selectedNote()) {
        // Each control shows its change as it's dragged, on every selected
        // note; letting go makes it one command on the clip.
        const int selected = model.selectionSize();
        if (selected > 1) ImGui::TextDisabled("%d notes selected", selected);
        // Velocity is a knob, the plugins' way: caption above, value below.
        const float em   = ImGui::GetFontSize();
        const float knobD = 3.2f * em;
        const float col   = ImGui::GetContentRegionAvail().x;
        const float x     = ImGui::GetCursorPosX();
        caption("Velocity", col, colour::kNote);
        ImGui::SetCursorPosX(x + (col - knobD) / 2);
        float v = static_cast<float>(n->velocity - GridModel::kMinVelocity) /
                  (GridModel::kMaxVelocity - GridModel::kMinVelocity);
        if (knob("##velocity", v, 99.0f / 126, knobD, colour::kNote)) {
            model.setSelectedVelocity(GridModel::kMinVelocity +
                                      static_cast<int>(std::lround(v * (GridModel::kMaxVelocity - GridModel::kMinVelocity))));
            editPending = true;
        }
        velocityRect = lastItem();
        ImGui::SetCursorPosX(x);
        valueBox("##velocityValue", std::to_string(model.selectedNote()->velocity), col);

        // Start and length in sixteenths, moved by whole sixteenths: a
        // recorded note keeps its offset from the grid.
        constexpr Tick step = GridModel::kStep;
        ImGui::PushItemWidth(-4.5f * em);
        const int shownStart  = static_cast<int>((n->start + step / 2) / step);
        const int shownLength = std::max(1, static_cast<int>((n->length + step / 2) / step));
        int pitch = n->pitch, start = shownStart, length = shownLength;
        if (sliderRow("Pitch", pitch, 0, model.rows() - 1, colour::kNote)) {
            model.moveSelected(n->start, pitch);
            editPending = true;
        }
        if (sliderRow("Start", start, 0, std::max(0, model.steps - length), colour::kNote)) {
            model.moveSelected(n->start + (start - shownStart) * step, n->pitch);
            editPending = true;
        }
        if (sliderRow("Length", length, 1, std::max(1, model.steps - start), colour::kNote)) {
            model.setSelectedLength(n->length + (length - shownLength) * step);
            editPending = true;
        }
        ImGui::PopItemWidth();
        notePitch(*model.selectedNote());
        if (ImGui::Button(selected > 1 ? "Delete notes" : "Delete note")) {
            model.deleteSelected();
            model.commit();
        }
    } else {
        ImGui::TextDisabled("No note selected.");
        ImGui::TextDisabled("Click the grid to add one.");
        velocityRect = {};
    }
    if (editPending && !ImGui::IsAnyItemActive()) {
        editPending = false;
        model.commit();
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
