#pragma once

#include <string>

#include "imgui.h"

// Umpire's own controls in the house look (roadmap.md, milestone 4; KeyboardEDO's
// decisions.md, "Look and feel"): PlainOldSynth's glowing knobs, a linear slider
// to go with them, value boxes and section boxes, in Umpire's colours. Copied
// from KeyboardEDO's src/app/Widgets.{h,cpp} (42f0948), not shared
// (decisions.md, "Milestone 4"); only the namespace and the colours differ.
namespace umpire::ui {

// Umpire's colours (provisional, for the author to settle: docs/skeleton.md):
// graphite panels, sections a step lighter, wells darker, and two lights, an
// umpire's amber for what's on, active or selected, and a teal for notes.
namespace colour {
inline const ImVec4 kPanel{26 / 255.f, 28 / 255.f, 33 / 255.f, 1};       // panels' background
inline const ImVec4 kSection{36 / 255.f, 39 / 255.f, 46 / 255.f, 1};     // group boxes, a step lighter
inline const ImVec4 kWell{17 / 255.f, 18 / 255.f, 22 / 255.f, 1};        // value boxes, slots, fields
inline const ImVec4 kSlate{104 / 255.f, 110 / 255.f, 124 / 255.f, 1};    // borders, faint text
inline const ImVec4 kAccent{240 / 255.f, 170 / 255.f, 50 / 255.f, 1};    // amber: on, active, selected
inline const ImVec4 kNote{72 / 255.f, 182 / 255.f, 196 / 255.f, 1};      // teal: notes and their controls
inline const ImVec4 kRecord{232 / 255.f, 72 / 255.f, 72 / 255.f, 1};     // red: armed to record
} // namespace colour

// The panels' ImGui style: the graphite for backgrounds and frames, the accent
// for what's on or active. Colours only; sizes are the widget's.
void applyTheme(ImGuiStyle& style);

// A glowing knob, `diameter` across, 0..1: dragging up turns it up, wherever
// the pointer goes (Ctrl or Cmd: ten times finer); a double click sets it to
// `start`. `steps` above 1: it stops at that many evenly spaced places.
// Returns true when it changed.
bool knob(const char* id, float& value, float start, float diameter, const ImVec4& accent, int steps = 0);

// A linear slider from `lo` to `hi`, `size` across and high: vertical when
// taller than wide. A recessed slot, lit from `rest` to the value, and a
// glowing thumb. A drag moves it as far as the pointer moves, wherever it's
// grabbed, so a click doesn't make it jump. `spring`: back to `rest` when let
// go (a bend wheel). Returns true when it changed.
bool slider(const char* id, float& value, float lo, float hi, float rest, ImVec2 size, const ImVec4& accent,
            bool spring = false);

// A horizontal slider in a settings row, as ImGui's SliderInt is laid out: the
// slider and a value box, the item width, then `label` (its text up to "##").
bool sliderRow(const char* label, int& value, int lo, int hi, const ImVec4& accent);

// A caption centred in the next `width` points, tinted toward `accent`.
void caption(const char* text, float width, const ImVec4& accent);

// A value box: `text` in a dark rounded box, centred in the next `width`
// points. Returns true when clicked (to type a value).
bool valueBox(const char* id, const std::string& text, float width);

// A section of a panel, as the plugins' group boxes: a rounded box a shade
// lighter than the panel, its title in `accent`. Collapsible when
// `collapsible`, and then returns whether it's open; call endSection()
// whatever it returns.
bool beginSection(const char* title, const ImVec4& accent, bool collapsible);
void endSection();

// --- Umpire's own, in the same look (not in KeyboardEDO) ---

// A button that's on or off (a track's arm, mute and solo): a dark well when
// off, lit in `colour` with a soft glow when on, `text` centred. Returns true
// when clicked.
bool toggle(const char* id, const char* text, bool on, const ImVec4& colour, ImVec2 size);

// An activity light, `diameter` across at the cursor: the knob's glow in
// `colour` when lit, a dark well when not. Takes no input.
void light(bool lit, const ImVec4& colour, float diameter);

// A transport button: a toggle (above) with a drawn glyph instead of text.
enum class Glyph { Play, Stop, Record, ToStart };
bool transportButton(const char* id, Glyph glyph, bool on, const ImVec4& colour, ImVec2 size);

// A badge at the cursor, for state that isn't the default (ui-direction.md,
// "Hide controls, never hide state"): `text` in a small pill of `colour`, on
// the current line. Takes no input; returns its width.
float badge(const char* text, const ImVec4& colour);

} // namespace umpire::ui
