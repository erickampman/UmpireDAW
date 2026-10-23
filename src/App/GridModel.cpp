#include "App/GridModel.h"

#include <algorithm>
#include <cmath>

namespace umpire {

GridNote* GridModel::selectedNote() noexcept {
    return selected >= 0 && selected < static_cast<int>(notes.size()) ? &notes[static_cast<std::size_t>(selected)]
                                                                       : nullptr;
}

const GridNote* GridModel::selectedNote() const noexcept {
    return const_cast<GridModel*>(this)->selectedNote();
}

int GridModel::noteAt(int pitch, int step) const noexcept {
    for (int i = static_cast<int>(notes.size()) - 1; i >= 0; --i) {
        const GridNote& n = notes[static_cast<std::size_t>(i)];
        if (n.pitch == pitch && step >= n.start && step < n.start + n.length) return i;
    }
    return -1;
}

int GridModel::addNote(int pitch, int step, int length, int velocity) {
    if (step < 0 || step >= steps) return -1;
    GridNote n;
    n.length   = std::clamp(length, 1, steps);
    n.start    = std::min(step, steps - n.length);
    n.pitch    = std::clamp(pitch, 0, rows() - 1);
    n.velocity = std::clamp(velocity, kMinVelocity, kMaxVelocity);
    notes.push_back(n);
    selected = static_cast<int>(notes.size()) - 1;
    edited();
    return selected;
}

void GridModel::select(int index) noexcept {
    const int next = index >= 0 && index < static_cast<int>(notes.size()) ? index : -1;
    if (next == selected) return;
    selected = next;
    touch();
}

void GridModel::deleteSelected() {
    if (selectedNote() == nullptr) return;
    notes.erase(notes.begin() + selected);
    selected = -1;
    edited();
}

void GridModel::clear() {
    notes.clear();
    selected = -1;
    edited();
}

void GridModel::newClip() {
    clear();
    clipName = "Untitled clip";
}

void GridModel::moveSelected(int start, int pitch) {
    GridNote* n = selectedNote();
    if (n == nullptr) return;
    const int s = std::clamp(start, 0, steps - n->length), p = std::clamp(pitch, 0, rows() - 1);
    if (s == n->start && p == n->pitch) return;
    n->start = s;
    n->pitch = p;
    edited();
}

void GridModel::transposeSelected(int by) {
    if (const GridNote* n = selectedNote()) moveSelected(n->start, n->pitch + by);
}

void GridModel::setSelectedVelocity(int velocity) {
    GridNote* n = selectedNote();
    if (n == nullptr) return;
    const int v = std::clamp(velocity, kMinVelocity, kMaxVelocity);
    if (v == n->velocity) return;
    n->velocity = v;
    edited();
}

int GridModel::rowFor(std::uint16_t pitch79) const noexcept {
    const double steps = (pitch79 / 512.0 - referenceNote) * edo / 12.0;
    return std::clamp(referenceRow() + static_cast<int>(std::lround(steps)), 0, rows() - 1);
}

void GridModel::setTuning(int e, int reference) {
    e         = std::clamp(e, Scale::kMinEdo, Scale::kMaxEdo);
    reference = std::clamp(reference, 0, 127);
    if (e == edo && reference == referenceNote) return;
    // Edited notes keep their pitch, on the new tuning's nearest row.
    std::vector<double> pitches(static_cast<std::size_t>(rows()));
    for (int r = 0; r < rows(); ++r) pitches[static_cast<std::size_t>(r)] = rowSemitones(r);
    edo           = e;
    referenceNote = reference;
    if (shown) {   // still the clip, unedited: laid out again
        layOut();
        return;
    }
    for (GridNote& n : notes) n.pitch = rowFor(static_cast<std::uint16_t>(std::lround(pitches[static_cast<std::size_t>(n.pitch)] * 512)));
    touch();
}

void GridModel::showClip(std::shared_ptr<const Clip> source) {
    if (!source) return;
    shown           = std::move(source);
    edo             = shown->scale.edo;
    referenceNote   = shown->scale.referenceNote;
    scaleFit        = shown->scaleFit;
    scaleCandidates = shown->scaleCandidates;
    layOut();
}

void GridModel::layOut() {
    const Clip&    clip = *shown;
    constexpr Tick step = kTicksPerQuarter / 4;
    const auto     steps16 = [](Tick t) { return static_cast<int>((t + step / 2) / step); };
    clipName = clip.name;
    steps    = std::max(16, (steps16(clip.length) + 15) / 16 * 16);
    notes.clear();
    for (const NoteSpan& n : notesOf(clip.events, clip.length)) {
        GridNote g;
        g.pitch    = rowFor(n.pitch79);
        g.start    = std::min(steps16(n.start), steps - 1);
        g.length   = std::clamp(steps16(n.start + n.length) - g.start, 1, steps - g.start);
        g.velocity = std::clamp(n.velocity >> 9, kMinVelocity, kMaxVelocity);
        notes.push_back(g);
    }
    selected = -1;
    touch();
}

void GridModel::setSelectedLength(int length) {
    GridNote* n = selectedNote();
    if (n == nullptr) return;
    const int l = std::clamp(length, 1, steps - n->start);
    if (l == n->length) return;
    n->length = l;
    edited();
}

} // namespace umpire
