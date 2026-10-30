#include "App/Instruments.h"

#include <algorithm>

namespace umpire {

namespace {
// A separator that won't appear in a path or a plugin id, unlike ':' (ALSA
// ids already use it) or '/' (paths do).
constexpr char        kSeparator[] = "\x1F";
constexpr const char* kPrefix      = "plugin";
} // namespace

Instruments::Instruments(uw::PluginFactory& f) : factory(f) {}

bool Instruments::rescan() {
    std::vector<uw::PluginDescriptor> scanned = factory.scan();
    std::vector<uw::PluginDescriptor> next;
    for (uw::PluginDescriptor& d : scanned)
        if (d.kind == uw::PluginKind::Instrument) next.push_back(std::move(d));
    std::sort(next.begin(), next.end(), [](const uw::PluginDescriptor& a, const uw::PluginDescriptor& b) {
        return a.name < b.name;
    });

    const bool changed = next.size() != found.size() ||
                          !std::equal(next.begin(), next.end(), found.begin(),
                                     [](const uw::PluginDescriptor& a, const uw::PluginDescriptor& b) {
                                         return a.path == b.path && a.id == b.id && a.name == b.name;
                                     });
    found = std::move(next);
    choiceList.clear();
    for (const uw::PluginDescriptor& d : found) choiceList.push_back(Choice{d.name, outputId(d)});
    return changed;
}

const uw::PluginDescriptor* Instruments::find(const std::string& outputId) const {
    if (!isPluginOutput(outputId)) return nullptr;
    for (const uw::PluginDescriptor& d : found)
        if (Instruments::outputId(d) == outputId) return &d;
    return nullptr;
}

std::string Instruments::outputId(const uw::PluginDescriptor& d) {
    return std::string(kPrefix) + kSeparator + d.path + kSeparator + d.id;
}

bool Instruments::isPluginOutput(const std::string& id) {
    return id.rfind(std::string(kPrefix) + kSeparator, 0) == 0;
}

} // namespace umpire
