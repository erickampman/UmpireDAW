#include "App/GridModel.h"

#include <algorithm>

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
    n.pitch    = std::clamp(pitch, 0, kPitches - 1);
    n.velocity = std::clamp(velocity, kMinVelocity, kMaxVelocity);
    notes.push_back(n);
    selected = static_cast<int>(notes.size()) - 1;
    touch();
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
    touch();
}

void GridModel::clear() {
    notes.clear();
    selected = -1;
    touch();
}

void GridModel::newClip() {
    clear();
    clipName = "Untitled clip";
}

void GridModel::moveSelected(int start, int pitch) {
    GridNote* n = selectedNote();
    if (n == nullptr) return;
    const int s = std::clamp(start, 0, steps - n->length), p = std::clamp(pitch, 0, kPitches - 1);
    if (s == n->start && p == n->pitch) return;
    n->start = s;
    n->pitch = p;
    touch();
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
    touch();
}

void GridModel::setSelectedLength(int length) {
    GridNote* n = selectedNote();
    if (n == nullptr) return;
    const int l = std::clamp(length, 1, steps - n->start);
    if (l == n->length) return;
    n->length = l;
    touch();
}

} // namespace umpire
