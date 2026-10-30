#pragma once

// Umpire's audio backend (milestone 8): JACK on Linux (via pipewire-jack),
// CoreAudio on macOS, through uw-core's AudioBackend seam, as
// PlatformMidiOutput.h picks the MIDI output. `name` is JACK's client name,
// as JACK and PipeWire show it; macOS ignores it (CoreAudio has no
// client-name concept) and opens the system's default output device.

#include <memory>
#include <string>

#include "uw/audio/AudioBackend.h"

#ifdef __APPLE__
#include "uw/audio/CoreAudioBackend.h"
#else
#include "uw/audio/JackBackend.h"
#endif

namespace umpire {

inline std::unique_ptr<uw::AudioBackend> createAudioBackend(const std::string& name) {
#ifdef __APPLE__
    (void)name;
    auto backend = std::make_unique<uw::CoreAudioBackend>();
    backend->setAggregateIdentity("Umpire", "com.unlikelyware.umpire");
    return backend;
#else
    return std::make_unique<uw::JackBackend>(name);
#endif
}

} // namespace umpire
