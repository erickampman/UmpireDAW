#pragma once

// The CLAP instruments a track can play to (milestone 8, docs/plugins.md):
// scanned through uw-core's plugin seam (main.cpp gives a real
// uw::ClapPluginFactory; tests give a fake uw::PluginFactory), filtered to
// uw::PluginKind::Instrument so note effects and bare audio effects don't
// show up as a track's output (they aren't rack rows yet). A track's output
// names one by Instruments::outputId(); AudioEngine instantiates it when the
// track is given that output, as it opens a MIDI device by its id.

#include <string>
#include <vector>

#include "uw/plugin/PluginDescriptor.h"
#include "uw/plugin/PluginFactory.h"

namespace umpire {

class Instruments {
public:
    // One instrument a track can play to.
    struct Choice {
        std::string label;   // the plugin's name
        std::string id;      // Instruments::outputId()'s result
        bool operator==(const Choice&) const = default;
    };

    // Starts empty: rescan() does the work, and it touches real plugin
    // files (ClapPluginFactory loads each one it finds to read its
    // descriptors), so the owner calls it where that's safe -- not during
    // umpire-daw --self-test or a ctest suite (App/main.cpp calls it where
    // it calls AudioEngine::start(), after the self-test's early return).
    explicit Instruments(uw::PluginFactory& factory);

    // Rescans the factory's locations (main thread; not real-time: it reads
    // the filesystem, and loads what it finds). Returns true if the list of
    // instruments changed.
    bool rescan();

    const std::vector<Choice>& choices() const noexcept { return choiceList; }

    // The descriptor an output id names, or null if it isn't a plugin
    // output, or isn't a plugin found by the last rescan() (not installed).
    const uw::PluginDescriptor* find(const std::string& outputId) const;

    // A track's output, as an opaque id carrying the plugin's path and its
    // id within it (ClapPluginFactory::scanFile can hold several).
    static std::string outputId(const uw::PluginDescriptor& desc);
    static bool         isPluginOutput(const std::string& outputId);

private:
    uw::PluginFactory&                factory;
    std::vector<uw::PluginDescriptor> found;
    std::vector<Choice>               choiceList;
};

} // namespace umpire
