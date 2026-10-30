#include "App/MidiOutputs.h"

namespace umpire {

MidiOutputs::MidiOutputs(uw::MidiOutput* out, std::vector<std::string> own)
    : output(out), ownClients(std::move(own)) {
    if (output != nullptr) seenChanges = output->changes();
    refreshChoices();
}

MidiOutputs::~MidiOutputs() {
    if (output == nullptr) return;
    for (const auto& [handle, o] : opened) output->close(handle);
}

bool MidiOutputs::syncDevices() {
    if (output == nullptr) return false;
    const std::uint64_t changes = output->changes();
    if (changes == seenChanges) return false;
    seenChanges = changes;
    for (auto& [handle, o] : opened) o.connected = output->sync(handle);
    refreshChoices();
    ++generations;
    return true;
}

uw::MidiDestinationHandle MidiOutputs::open(const std::string& id) {
    if (output == nullptr || own(id)) return uw::kInvalidMidiDestination;
    bool present = false;
    for (const Choice& c : choiceList) present = present || c.id == id;
    if (!present) return uw::kInvalidMidiDestination;   // not now: no complaint from the platform
    const uw::MidiDestinationHandle handle = output->open(id);
    if (handle != uw::kInvalidMidiDestination) opened[handle] = Open{id, true};
    return handle;
}

void MidiOutputs::close(uw::MidiDestinationHandle handle) {
    if (opened.erase(handle) != 0) output->close(handle);
}

bool MidiOutputs::connected(uw::MidiDestinationHandle handle) const {
    const auto it = opened.find(handle);
    return it != opened.end() && it->second.connected;
}

uw::MidiProtocol MidiOutputs::protocol(uw::MidiDestinationHandle handle) const {
    return opened.count(handle) != 0 ? output->protocol(handle) : uw::MidiProtocol::Midi1;
}

bool MidiOutputs::schedules(uw::MidiDestinationHandle handle) const {
    return opened.count(handle) != 0 && output->schedules(handle);
}

void MidiOutputs::sendAt(uw::MidiDestinationHandle handle, const uw::MidiMessage* messages, std::size_t count,
                         uw::Nanoseconds when) {
    if (count == 0 || !connected(handle)) return;
    if (output->sendAt(handle, messages, count, when)) sentCount += count;
}

void MidiOutputs::cancel(uw::MidiDestinationHandle handle) {
    if (opened.count(handle) != 0) output->cancelScheduled(handle);
}

std::string MidiOutputs::label(const std::string& id, bool* connected) const {
    for (const Choice& c : choiceList)
        if (c.id == id) {
            if (connected != nullptr) *connected = true;
            return c.label;
        }
    if (connected != nullptr) *connected = false;
    return id;
}

// Hardware first, as the inputs' choices: it's what a player picks.
void MidiOutputs::refreshChoices() {
    choiceList.clear();
    if (output == nullptr) return;
    const std::vector<uw::MidiDestinationInfo> found = output->enumerate();
    for (const bool hardware : {true, false})
        for (const uw::MidiDestinationInfo& info : found)
            if (info.isHardware == hardware && !own(info.id))
                choiceList.push_back(Choice{info.name.empty() ? info.id : info.name, info.id, hardware});
}

bool MidiOutputs::own(const std::string& id) const {
    for (const std::string& client : ownClients)
        if (id == client || id.rfind(client + ":", 0) == 0) return true;
    return false;
}

} // namespace umpire
