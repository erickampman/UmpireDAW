#pragma once

// The grid window's ImGui panels, over its canvas: the menu bar and the
// inspector at the right, in the house widgets (Widgets.h). Umpire's ImGuiWidget does the
// routing and the drawing on demand.

#include "App/GridModel.h"
#include "Ui/ImGuiWidget.h"
#include "Ui/Probe.h"

namespace umpire::ui {

class GridPanels final : public ImGuiWidget {
public:
    GridPanels(DGL_NAMESPACE::Widget* parent, GridModel& model, Probe& probe);

    // The area the canvas may draw in (window pixels): below the menu bar and
    // left of the inspector. Known after the first frame.
    float menuBarHeight() const noexcept { return menuH; }
    float panelWidth() const noexcept { return panelW; }

    bool quitRequested  = false;
    bool closeRequested = false;   // File > Close window
    bool showDemo      = false;
    bool showDiagnostics = false;

    // For the self-test: where things were drawn in the last frame (window pixels).
    struct Rect {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        float cx() const { return (x0 + x1) / 2; }
        float cy() const { return (y0 + y1) / 2; }
    };
    Rect editMenuRect, clearAllItemRect, nameFieldRect, velocityRect;
    bool editMenuOpen = false;

protected:
    void onImGuiDisplay() override;
    void onStyle(ImGuiStyle& style) override;
    void routed(Input input, bool toImGui) override;

private:
    void menuBar();
    void inspector(float width, float height);
    void notePitch(const GridNote& note);

    GridModel& model;
    Probe&     probe;
    float      menuH  = 0;
    float      panelW = 0;
    char       nameBuf[128] = {};
    bool       tuningPending = false;   // a tuning dragged, for the clip when let go
};

} // namespace umpire::ui
