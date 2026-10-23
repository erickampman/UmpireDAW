#include "App/MidiInputs.h"

#include <cstdio>

namespace umpire {

MidiInputs::MidiInputs(uw::MidiInput* in) : input(in) {
    if (input != nullptr) input->setSink(this);
    refreshChoices();
}

MidiInputs::~MidiInputs() {
    if (input == nullptr) return;
    for (const auto& [handle, source] : open) input->close(handle);
    input->setSink(nullptr);
}

void MidiInputs::openAll() {
    if (input == nullptr) return;
    seenChanges = input->changes();
    const std::pair<const char*, uw::MidiProtocol> virtuals[] = {{kVirtualMidi2, uw::MidiProtocol::Midi2},
                                                                 {kVirtualMidi1, uw::MidiProtocol::Midi1}};
    for (const auto& [name, protocol] : virtuals) {
        const uw::MidiSourceHandle handle = input->openVirtual(name, protocol);
        if (handle != uw::kInvalidMidiSource) open[handle] = Open{{MidiSourceRef::Kind::Virtual, name}, name, true};
    }
    // Every hardware input, as a live host should (StageHand's rule); an app's
    // own source (KeyEDO's) is chosen by its row, or connects to a virtual input.
    for (const uw::MidiSourceInfo& info : input->enumerate()) {
        if (!info.isHardware) continue;
        const uw::MidiSourceHandle handle = input->open(info.id);
        if (handle != uw::kInvalidMidiSource)
            open[handle] = Open{{MidiSourceRef::Kind::Device, info.id}, info.name.empty() ? info.id : info.name, true};
    }
    refreshChoices();
}

bool MidiInputs::syncDevices() {
    if (input == nullptr) return false;
    const std::uint64_t changes = input->changes();
    if (changes == seenChanges) return false;
    seenChanges = changes;
    for (auto& [handle, source] : open) source.connected = input->sync(handle);
    for (const uw::MidiSourceInfo& info : input->enumerate()) {
        if (!info.isHardware || handleFor({MidiSourceRef::Kind::Device, info.id}) != uw::kInvalidMidiSource) continue;
        const uw::MidiSourceHandle handle = input->open(info.id);
        if (handle != uw::kInvalidMidiSource)
            open[handle] = Open{{MidiSourceRef::Kind::Device, info.id}, info.name.empty() ? info.id : info.name, true};
    }
    refreshChoices();
    return true;
}

void MidiInputs::ensureOpen(const MidiSourceRef& ref) {
    if (input == nullptr || ref.kind != MidiSourceRef::Kind::Device || handleFor(ref) != uw::kInvalidMidiSource) return;
    for (const uw::MidiSourceInfo& info : enumerated)
        if (info.id == ref.name) {
            const uw::MidiSourceHandle handle = input->open(info.id);
            if (handle != uw::kInvalidMidiSource)
                open[handle] = Open{ref, info.name.empty() ? info.id : info.name, true};
            return;
        }
}

void MidiInputs::refreshChoices() {
    enumerated = input != nullptr ? input->enumerate() : std::vector<uw::MidiSourceInfo>{};
    choiceList.clear();
    choiceList.push_back(Choice{"All inputs", MidiSourceRef{}, false});
    choiceList.push_back(Choice{"No input", MidiSourceRef{MidiSourceRef::Kind::None, {}}, false});
    // Hardware first: it's what a player picks; then software sources, then
    // the virtual inputs other apps send to.
    for (const bool hardware : {true, false})
        for (const uw::MidiSourceInfo& info : enumerated)
            if (info.isHardware == hardware)
                choiceList.push_back(Choice{info.name.empty() ? info.id : info.name,
                                            MidiSourceRef{MidiSourceRef::Kind::Device, info.id}, hardware});
    for (const char* name : {kVirtualMidi2, kVirtualMidi1})
        choiceList.push_back(Choice{std::string("Umpire ") + name, MidiSourceRef{MidiSourceRef::Kind::Virtual, name}, false});
}

void MidiInputs::drain(std::vector<Received>& out) {
    Received r;
    while (queue.pop(r)) out.push_back(r);
}

void MidiInputs::onMidi(uw::MidiSourceHandle source, const uw::TimedMidiMessage& msg) noexcept {
    if (!queue.push(Received{source, msg})) droppedCount.fetch_add(1, std::memory_order_relaxed);
}

uw::MidiSourceHandle MidiInputs::handleFor(const MidiSourceRef& ref) const {
    for (const auto& [handle, source] : open)
        if (source.ref == ref) return handle;
    return uw::kInvalidMidiSource;
}

bool MidiInputs::matches(const MidiSourceRef& ref, uw::MidiSourceHandle source) const {
    switch (ref.kind) {
    case MidiSourceRef::Kind::All: return source != uw::kInvalidMidiSource;
    case MidiSourceRef::Kind::None: return false;
    default: return source != uw::kInvalidMidiSource && handleFor(ref) == source;
    }
}

std::string MidiInputs::sourceName(uw::MidiSourceHandle source) const {
    const auto it = open.find(source);
    return it != open.end() ? it->second.name : "?";
}

std::string MidiInputs::label(const MidiSourceRef& ref, bool* connected) const {
    if (connected != nullptr) *connected = true;
    for (const Choice& c : choiceList)
        if (c.ref == ref) {
            if (const auto it = open.find(handleFor(ref)); it != open.end() && connected != nullptr)
                *connected = it->second.connected;
            return c.label;
        }
    if (connected != nullptr) *connected = false;
    return ref.name + " (not connected)";
}

} // namespace umpire
