#include "App/GridModel.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "uw/ump/Ump.h"

namespace umpire {

namespace {

Tick nearestStep(Tick t) {
    return (t + kGridStep / 2) / kGridStep * kGridStep;
}

std::uint16_t velocity16(int velocity) {
    return static_cast<std::uint16_t>(uw::ump::scaleUp(static_cast<std::uint32_t>(velocity), 7, 16));
}

} // namespace

GridNote* GridModel::selectedNote() noexcept {
    return focus >= 0 && focus < static_cast<int>(notes.size()) ? &notes[static_cast<std::size_t>(focus)] : nullptr;
}

const GridNote* GridModel::selectedNote() const noexcept {
    return const_cast<GridModel*>(this)->selectedNote();
}

int GridModel::selectionSize() const noexcept {
    return static_cast<int>(std::count_if(notes.begin(), notes.end(), [](const GridNote& n) { return n.selected; }));
}

int GridModel::noteAt(int pitch, Tick tick) const noexcept {
    for (int i = static_cast<int>(notes.size()) - 1; i >= 0; --i) {
        const GridNote& n = notes[static_cast<std::size_t>(i)];
        if (n.pitch == pitch && tick >= n.start && tick < n.start + n.length) return i;
    }
    return -1;
}

std::uint16_t GridModel::rowPitch79(int row) const noexcept {
    return static_cast<std::uint16_t>(std::clamp(std::lround(rowSemitones(row) * 512), 0L, 65535L));
}

int GridModel::rowFor(std::uint16_t pitch79) const noexcept {
    const double s = (pitch79 / 512.0 - referenceNote) * edo / 12.0;
    return std::clamp(referenceRow() + static_cast<int>(std::lround(s)), 0, rows() - 1);
}

// --- selecting ----------------------------------------------------------------------

void GridModel::select(int index) noexcept {
    const int next = index >= 0 && index < static_cast<int>(notes.size()) ? index : -1;
    bool      diff = next != focus;
    for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
        GridNote& n = notes[static_cast<std::size_t>(i)];
        diff |= n.selected != (i == next);
        n.selected = i == next;
    }
    focus = next;
    if (diff) touch();
}

void GridModel::toggle(int index) noexcept {
    if (index < 0 || index >= static_cast<int>(notes.size())) return;
    GridNote& n = notes[static_cast<std::size_t>(index)];
    n.selected  = !n.selected;
    if (n.selected) {
        focus = index;
    } else if (focus == index) {
        focus = -1;
        for (int i = static_cast<int>(notes.size()) - 1; i >= 0 && focus < 0; --i)
            if (notes[static_cast<std::size_t>(i)].selected) focus = i;
    }
    touch();
}

void GridModel::selectIn(int rowLow, int rowHigh, Tick from, Tick to, bool add) noexcept {
    if (rowLow > rowHigh) std::swap(rowLow, rowHigh);
    if (from > to) std::swap(from, to);
    bool diff = false;
    int  last = -1;
    for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
        GridNote&  n      = notes[static_cast<std::size_t>(i)];
        const bool inside = n.pitch >= rowLow && n.pitch <= rowHigh && n.start < to && from < n.start + n.length;
        const bool now    = inside || (add && n.selected);
        if (inside) last = i;
        diff |= now != n.selected;
        n.selected = now;
    }
    if (focus < 0 || !notes[static_cast<std::size_t>(focus)].selected) {
        diff |= focus != last;
        focus = last;
    }
    if (diff) touch();
}

void GridModel::selectAll() noexcept {
    bool diff = false;
    for (GridNote& n : notes) {
        diff |= !n.selected;
        n.selected = true;
    }
    const int next = notes.empty() ? -1 : focus >= 0 ? focus : static_cast<int>(notes.size()) - 1;
    diff |= next != focus;
    focus = next;
    if (diff) touch();
}

// --- edits ----------------------------------------------------------------------------

int GridModel::addNote(int pitch, int step, int lengthSteps, int velocity) {
    if (step < 0 || step >= steps) return -1;
    for (GridNote& n : notes) n.selected = false;
    GridNote n;
    n.length     = std::clamp(lengthSteps, 1, steps) * kStep;
    n.start      = std::min(step * kStep, endTick() - n.length);
    n.pitch      = std::clamp(pitch, 0, rows() - 1);
    n.velocity   = std::clamp(velocity, kMinVelocity, kMaxVelocity);
    n.velocity16 = velocity16(n.velocity);
    n.pitch79    = rowPitch79(n.pitch);
    n.clipRow    = n.pitch;
    n.selected   = true;
    notes.push_back(n);
    focus = static_cast<int>(notes.size()) - 1;
    edited();
    return focus;
}

void GridModel::deleteSelected() {
    const auto gone = std::erase_if(notes, [](const GridNote& n) { return n.selected; });
    if (gone == 0) return;
    focus = -1;
    edited();
}

void GridModel::clear() {
    if (notes.empty()) return;
    notes.clear();
    focus = -1;
    edited();
}

void GridModel::newClip() {
    shown.reset();
    steps = 64;
    original.clear();
    notes.clear();
    focus    = -1;
    pending  = false;
    clipName = "Untitled clip";
    touch();
}

void GridModel::moveBy(const std::vector<GridNote>& from, Tick ticks, int by) {
    if (from.size() != notes.size()) return;
    // The selection moves as one, kept inside the grid.
    Tick first = endTick(), last = 0;
    int  low = rows(), high = -1;
    for (const GridNote& n : from) {
        if (!n.selected) continue;
        first = std::min(first, n.start), last = std::max(last, n.start + n.length);
        low = std::min(low, n.pitch), high = std::max(high, n.pitch);
    }
    if (high < 0) return;
    ticks = std::clamp(ticks, -first, std::max<Tick>(endTick() - last, -first));
    by    = std::clamp(by, -low, std::max(rows() - 1 - high, -low));
    bool diff = false;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (!from[i].selected) continue;
        const Tick s = from[i].start + ticks;
        const int  p = from[i].pitch + by;
        diff |= s != notes[i].start || p != notes[i].pitch;
        notes[i].start = s;
        notes[i].pitch = p;
    }
    if (diff) edited();
}

void GridModel::beginDrag() {
    grabbed = notes;
}

void GridModel::dragMove(Tick ticks, int by, bool toGrid) {
    if (grabbed.size() != notes.size() || focus < 0) return;
    if (toGrid) {   // the focus note's start onto the nearest sixteenth
        const Tick start = grabbed[static_cast<std::size_t>(focus)].start;
        ticks            = nearestStep(start + ticks) - start;
    }
    moveBy(grabbed, ticks, by);
}

void GridModel::dragResize(Tick ticks, bool snap, bool toGrid) {
    if (grabbed.size() != notes.size() || focus < 0) return;
    if (toGrid) {   // the focus note's end onto the nearest sixteenth
        const GridNote& f = grabbed[static_cast<std::size_t>(focus)];
        ticks             = nearestStep(f.start + f.length + ticks) - (f.start + f.length);
    }
    const Tick shortest = snap ? kStep : 1;
    bool       diff     = false;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (!grabbed[i].selected) continue;
        const Tick l = std::clamp(grabbed[i].length + ticks, std::min(shortest, grabbed[i].length),
                                  std::max<Tick>(endTick() - grabbed[i].start, 1));
        diff |= l != notes[i].length;
        notes[i].length = l;
    }
    if (diff) edited();
}

void GridModel::nudge(Tick ticks, int by) {
    moveBy(std::vector<GridNote>(notes), ticks, by);
}

void GridModel::moveSelected(Tick start, int pitch) {
    const GridNote* f = selectedNote();
    if (f == nullptr) return;
    moveBy(std::vector<GridNote>(notes), start - f->start, pitch - f->pitch);
}

void GridModel::transposeSelected(int by) {
    nudge(0, by);
}

void GridModel::setSelectedLength(Tick length) {
    const GridNote* f = selectedNote();
    if (f == nullptr) return;
    const Tick by   = length - f->length;
    bool       diff = false;
    for (GridNote& n : notes) {
        if (!n.selected) continue;
        const Tick l = std::clamp(n.length + by, Tick{1}, std::max<Tick>(endTick() - n.start, 1));
        diff |= l != n.length;
        n.length = l;
    }
    if (diff) edited();
}

void GridModel::setSelectedVelocity(int velocity) {
    const int v    = std::clamp(velocity, kMinVelocity, kMaxVelocity);
    bool      diff = false;
    for (GridNote& n : notes) {
        if (!n.selected || n.velocity == v) continue;
        n.velocity   = v;
        n.velocity16 = velocity16(v);
        diff         = true;
    }
    if (diff) edited();
}

void GridModel::quantizeSelected() {
    bool diff = false;
    for (GridNote& n : notes) {
        if (!n.selected) continue;
        Tick s = nearestStep(n.start);
        if (s + n.length > endTick()) s = std::max<Tick>(0, (endTick() - n.length) / kStep * kStep);
        diff |= s != n.start;
        n.start = s;
    }
    if (diff) edited();
}

void GridModel::setClipName(const std::string& name) {
    if (name.empty() || name == clipName) return;
    if (renameClip) {
        renameClip(name);   // the clip comes back renamed (showClip)
        return;
    }
    clipName = name;
    touch();
}

// --- commit ----------------------------------------------------------------------------

std::string GridModel::pendingChanges(std::vector<NoteChange>& out, std::vector<NewNote>& added) const {
    out.clear();
    added.clear();
    std::map<std::size_t, const GridNote*> was;
    for (const GridNote& o : original) was[o.on] = &o;
    int moved = 0, resized = 0, velocity = 0, removed = 0;
    std::map<std::size_t, bool> kept;
    for (const GridNote& n : notes) {
        if (n.on == NoteSpan::npos) continue;
        kept[n.on] = true;
        const auto it = was.find(n.on);
        if (it == was.end()) continue;
        const GridNote& o = *it->second;
        const bool time = n.start != o.start, row = n.pitch != o.clipRow, length = n.length != o.length,
                   loud = n.velocity16 != o.velocity16;
        moved += time || row;
        resized += length;
        velocity += loud;
        if (!time && !row && !length && !loud) continue;
        out.push_back({n.on, false, n.start - origin, n.length, row ? rowPitch79(n.pitch) : o.pitch79, n.velocity16});
    }
    for (const GridNote& o : original)
        if (!kept.contains(o.on)) {
            out.push_back({o.on, true, o.start - origin, o.length, o.pitch79, o.velocity16});
            ++removed;
        }
    for (const GridNote& n : notes)
        if (n.on == NoteSpan::npos) added.push_back({n.start - origin, n.length, rowPitch79(n.pitch), n.velocity16});

    const int count = static_cast<int>(out.size() + added.size());
    if (count == 0) return {};
    const char* s = count == 1 ? "" : "s";
    const int   kinds = (moved > 0) + (resized > 0) + (velocity > 0) + (removed > 0) + !added.empty();
    if (kinds > 1) return "Edit notes";
    if (!added.empty()) return std::string("Add note") + s;
    if (removed > 0) return std::string("Delete note") + s;
    if (velocity > 0) return "Set velocity";
    if (resized > 0) return std::string("Resize note") + s;
    return std::string("Move note") + s;
}

bool GridModel::commit(const std::string& name) {
    if (!editClip || !pending) return false;
    std::vector<NoteChange> changes;
    std::vector<NewNote>    added;
    std::string             label = pendingChanges(changes, added);
    if (label.empty()) {   // back where it started
        pending = false;
        touch();
        return false;
    }
    if (!name.empty()) label = name;
    // The selected notes stay selected in the new clip: the unchanged ones go
    // with the changes, as they are, so the edit says where they are now.
    std::vector<std::size_t> selectedChanges;   // into changes
    for (const GridNote& n : notes) {
        if (!n.selected || n.on == NoteSpan::npos) continue;
        const auto it = std::find_if(changes.begin(), changes.end(), [&](const NoteChange& c) { return c.on == n.on; });
        if (it != changes.end()) continue;
        changes.push_back({n.on, false, n.start - origin, n.length, n.pitch79, n.velocity16});
    }
    // Where the focus is among the placed notes: kept changes, then added.
    std::size_t focusPlaced = NoteSpan::npos, place = 0;
    for (const NoteChange& c : changes) {
        if (c.remove) continue;
        if (const GridNote* f = selectedNote(); f != nullptr && f->on == c.on) focusPlaced = place;
        ++place;
    }
    for (int i = 0; i < static_cast<int>(notes.size()); ++i)
        if (notes[static_cast<std::size_t>(i)].on == NoteSpan::npos) {
            if (i == focus) focusPlaced = place;
            ++place;
        }
    std::vector<bool> placedSelected;   // which placed notes were selected
    for (const NoteChange& c : changes) {
        if (c.remove) continue;
        const auto it = std::find_if(notes.begin(), notes.end(), [&](const GridNote& n) { return n.on == c.on; });
        placedSelected.push_back(it != notes.end() && it->selected);
    }
    for (const GridNote& n : notes)
        if (n.on == NoteSpan::npos) placedSelected.push_back(n.selected);

    const EditedClip made = editClip(label, changes, added);
    pending               = false;
    if (!made.clip) {   // it didn't take: the clip as it is
        std::vector<std::size_t> ons;
        std::size_t              focusOn = NoteSpan::npos;
        layOut(ons, focusOn);
        return false;
    }
    std::vector<std::size_t> select;
    std::size_t              focusOn = NoteSpan::npos;
    for (std::size_t p = 0; p < made.placed.size(); ++p) {
        if (p < placedSelected.size() && placedSelected[p]) select.push_back(made.placed[p]);
        if (p == focusPlaced) focusOn = made.placed[p];
    }
    shown = made.clip;
    layOut(select, focusOn);
    return true;
}

// --- the clip ---------------------------------------------------------------------------

void GridModel::setTuning(int e, int reference) {
    e         = std::clamp(e, Scale::kMinEdo, Scale::kMaxEdo);
    reference = std::clamp(reference, 0, 127);
    if (e == edo && reference == referenceNote) return;
    // Edited notes keep their pitch, on the new tuning's nearest row.
    std::vector<double> pitches(static_cast<std::size_t>(rows()));
    for (int r = 0; r < rows(); ++r) pitches[static_cast<std::size_t>(r)] = rowSemitones(r);
    edo           = e;
    referenceNote = reference;
    if (showingClip()) {   // still the clip, unedited: laid out again
        std::vector<std::size_t> ons;
        std::size_t              focusOn = NoteSpan::npos;
        selectedOns(ons, focusOn);
        layOut(ons, focusOn);
        return;
    }
    for (GridNote& n : notes) {
        n.pitch = rowFor(static_cast<std::uint16_t>(std::lround(pitches[static_cast<std::size_t>(n.pitch)] * 512)));
        if (n.fromClip) n.clipRow = rowFor(n.pitch79);
    }
    touch();
}

void GridModel::selectedOns(std::vector<std::size_t>& ons, std::size_t& focusOn) const {
    for (const GridNote& n : notes)
        if (n.selected && n.on != NoteSpan::npos) ons.push_back(n.on);
    focusOn = selectedNote() != nullptr ? selectedNote()->on : NoteSpan::npos;
}

void GridModel::showClip(std::shared_ptr<const Clip> source, const std::vector<std::size_t>* select) {
    if (!source) return;
    std::vector<std::size_t> ons;
    std::size_t              focusOn = NoteSpan::npos;
    if (select != nullptr) ons = *select;
    else if (shown && !pending && sameEvents(shown->events, source->events)) selectedOns(ons, focusOn);
    if (!shown) steps = 0;   // a new window's grid is as long as its clip
    shown           = std::move(source);
    pending         = false;
    edo             = shown->scale.edo;
    referenceNote   = shown->scale.referenceNote;
    scaleFit        = shown->scaleFit;
    scaleCandidates = shown->scaleCandidates;
    layOut(ons, focusOn);
}

void GridModel::release() {
    shown.reset();
    original.clear();
    grabbed.clear();
    notes.clear();
    pending  = false;
    focus    = -1;
    steps    = 0;
    clipName.clear();
    touch();
}

void GridModel::layOut(const std::vector<std::size_t>& selectOn, std::size_t focusOn) {
    const Clip& clip = *shown;
    clipName = clip.name;
    steps    = std::max({16, steps, static_cast<int>((origin + clip.length + 16 * kStep - 1) / (16 * kStep)) * 16});
    notes.clear();
    focus = -1;
    for (const NoteSpan& n : notesOf(clip.events, clip.length)) {
        GridNote g;
        g.pitch      = rowFor(n.pitch79);
        g.start      = origin + n.start;
        g.length     = std::max<Tick>(n.length, 1);
        g.velocity   = std::clamp(n.velocity >> 9, kMinVelocity, kMaxVelocity);
        g.fromClip   = true;
        g.on         = n.on;
        g.sentNote   = n.note;
        g.pitch79    = n.pitch79;
        g.pitchFrom  = n.from;
        g.velocity16 = n.velocity;
        g.clipRow    = g.pitch;
        g.selected   = std::find(selectOn.begin(), selectOn.end(), n.on) != selectOn.end();
        if (g.selected && (n.on == focusOn || focus < 0)) focus = static_cast<int>(notes.size());
        notes.push_back(g);
    }
    original = notes;
    grabbed.clear();
    touch();
}

} // namespace umpire
