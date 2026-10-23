#include "App/ViewModel.h"

#include <algorithm>

namespace umpire {

GridNote* ViewModel::selectedNote() noexcept {
    return selected >= 0 && selected < static_cast<int>(notes.size()) ? &notes[static_cast<std::size_t>(selected)]
                                                                       : nullptr;
}

const GridNote* ViewModel::selectedNote() const noexcept {
    return const_cast<ViewModel*>(this)->selectedNote();
}

int ViewModel::noteAt(int pitch, int step) const noexcept {
    for (int i = static_cast<int>(notes.size()) - 1; i >= 0; --i) {
        const GridNote& n = notes[static_cast<std::size_t>(i)];
        if (n.pitch == pitch && step >= n.start && step < n.start + n.length) return i;
    }
    return -1;
}

int ViewModel::addNote(int pitch, int step, int length, int velocity) {
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

void ViewModel::select(int index) noexcept {
    const int next = index >= 0 && index < static_cast<int>(notes.size()) ? index : -1;
    if (next == selected) return;
    selected = next;
    touch();
}

void ViewModel::deleteSelected() {
    if (selectedNote() == nullptr) return;
    notes.erase(notes.begin() + selected);
    selected = -1;
    touch();
}

void ViewModel::clear() {
    notes.clear();
    selected = -1;
    touch();
}

void ViewModel::newClip() {
    clear();
    clipName = "Untitled clip";
}

void ViewModel::moveSelected(int start, int pitch) {
    GridNote* n = selectedNote();
    if (n == nullptr) return;
    const int s = std::clamp(start, 0, steps - n->length), p = std::clamp(pitch, 0, kPitches - 1);
    if (s == n->start && p == n->pitch) return;
    n->start = s;
    n->pitch = p;
    touch();
}

void ViewModel::transposeSelected(int by) {
    if (const GridNote* n = selectedNote()) moveSelected(n->start, n->pitch + by);
}

void ViewModel::setSelectedVelocity(int velocity) {
    GridNote* n = selectedNote();
    if (n == nullptr) return;
    const int v = std::clamp(velocity, kMinVelocity, kMaxVelocity);
    if (v == n->velocity) return;
    n->velocity = v;
    touch();
}

void ViewModel::setSelectedLength(int length) {
    GridNote* n = selectedNote();
    if (n == nullptr) return;
    const int l = std::clamp(length, 1, steps - n->start);
    if (l == n->length) return;
    n->length = l;
    touch();
}

} // namespace umpire
