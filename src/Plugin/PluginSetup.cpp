#include "Plugin/PluginSetup.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstring>
#include <sstream>

#include "Ui/Zoom.h"

namespace umpire::plugin {

namespace {

std::string trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

constexpr std::uint32_t kVersion = 6;   // 1: the spike's one clip; 2: no flags; 3: no names, scales, choices; 4: no zoom; 5: no effects

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void putString(std::vector<std::uint8_t>& out, const std::string& s) {
    put32(out, static_cast<std::uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

// kHearOff (HE): Edit > Hear notes while editing turned off. Unset in every
// older state, so hearing is on there; an older build ignores it.
constexpr std::uint32_t kArmed = 1u, kSynthChosen = 2u, kHearOff = 4u;
constexpr std::uint32_t kBypassed = 1u;   // a MIDI effect's flags (NE)

} // namespace

Config parseConfig(const std::string& text) {
    Config             config;
    std::istringstream lines(text);
    std::string        line;
    while (std::getline(lines, line)) {
        if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = lower(trim(line.substr(0, eq)));
        const std::string value = trim(line.substr(eq + 1));
        if (key == "input") config.input = value;
        else if (key == "synth") config.synth = value;
        else if (key == "synth-id") config.synthId = value;
        else if (key == "record") config.record = lower(value);
    }
    return config;
}

RecordMode recordMode(const std::string& record, const std::string& hostName) {
    if (record == "host") return RecordMode::Host;
    if (record == "play") return RecordMode::Play;
    if (record == "off") return RecordMode::Off;
    const auto startsWith = [&hostName](const char* name) { return hostName.rfind(name, 0) == 0; };
    for (const char* name : {"MainStage", "StageHand"})
        if (startsWith(name)) return RecordMode::Off;
    for (const char* name : {"Logic Pro", "GarageBand"})
        if (startsWith(name)) return RecordMode::Play;
    return RecordMode::Host;
}

const char* recordModeName(RecordMode mode) {
    switch (mode) {
    case RecordMode::Host: return "host";
    case RecordMode::Play: return "play";
    case RecordMode::Off: return "off";
    }
    return "host";
}

int chooseSource(const std::vector<uw::MidiSourceInfo>& sources, const std::string& wanted) {
    const auto find = [&](auto&& pred) {
        const auto it = std::find_if(sources.begin(), sources.end(), pred);
        return it == sources.end() ? -1 : static_cast<int>(it - sources.begin());
    };
    if (lower(wanted) == "none") return -1;
    if (!wanted.empty())
        return find([&](const uw::MidiSourceInfo& s) { return contains(s.name, wanted) || contains(s.id, wanted); });
    if (const int i = find([](const uw::MidiSourceInfo& s) { return contains(s.name, "KeyboardEDO"); }); i >= 0) return i;
    if (const int i = find([](const uw::MidiSourceInfo& s) { return s.protocol == uw::MidiProtocol::Midi2; }); i >= 0) return i;
    return find([](const uw::MidiSourceInfo& s) { return s.isHardware; });
}

std::vector<std::uint8_t> encodeState(const SavedState& state) {
    std::vector<std::uint8_t> out = {'U', 'M', 'P', 'P'};
    put32(out, kVersion);
    put32(out, static_cast<std::uint32_t>(state.takes.size()));
    for (const SavedTake& take : state.takes) {
        const auto start = static_cast<std::uint64_t>(take.start);
        put32(out, static_cast<std::uint32_t>(start));
        put32(out, static_cast<std::uint32_t>(start >> 32));
        put32(out, static_cast<std::uint32_t>(take.clipFile.size()));
        out.insert(out.end(), take.clipFile.begin(), take.clipFile.end());
        putString(out, take.name);
        put32(out, static_cast<std::uint32_t>(take.scale.edo));
        put32(out, static_cast<std::uint32_t>(take.scale.referenceNote));
        put32(out, static_cast<std::uint32_t>(take.scaleFit));
    }
    put32(out, state.nextTakeNumber);
    putString(out, state.synthId);
    put32(out, static_cast<std::uint32_t>(state.synthState.size()));
    for (const std::byte b : state.synthState) out.push_back(static_cast<std::uint8_t>(b));
    put32(out, (state.armed ? kArmed : 0u) | (state.synthChosen ? kSynthChosen : 0u) | (state.hearNotes ? 0u : kHearOff));
    putString(out, state.synthPath);
    putString(out, state.synthName);
    put32(out, static_cast<std::uint32_t>(state.input.mode));
    putString(out, state.input.name);
    putString(out, state.input.id);
    put32(out, std::bit_cast<std::uint32_t>(state.zoom.lane));
    put32(out, std::bit_cast<std::uint32_t>(state.zoom.grid));
    put32(out, static_cast<std::uint32_t>(state.effects.size()));
    for (const SavedEffect& e : state.effects) {
        putString(out, e.id);
        putString(out, e.path);
        putString(out, e.name);
        put32(out, e.bypassed ? kBypassed : 0u);
        put32(out, static_cast<std::uint32_t>(e.state.size()));
        for (const std::byte b : e.state) out.push_back(static_cast<std::uint8_t>(b));
    }
    return out;
}

bool decodeState(const std::uint8_t* data, std::size_t size, SavedState& state, std::string& error) {
    std::size_t at  = 0;
    const auto  get32 = [&](std::uint32_t& v) {
        if (size - at < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(data[at + static_cast<std::size_t>(i)]) << (8 * i);
        at += 4;
        return true;
    };
    const auto bytes = [&](std::uint32_t n, const std::uint8_t*& p) {
        if (size - at < n) return false;
        p = data + at;
        at += n;
        return true;
    };
    const auto str = [&](std::string& out) {
        std::uint32_t      len = 0;
        const std::uint8_t* q  = nullptr;
        if (!get32(len) || !bytes(len, q)) return false;
        out.assign(reinterpret_cast<const char*>(q), len);
        return true;
    };
    state = SavedState{};
    if (size < 8 || std::memcmp(data, "UMPP", 4) != 0) {
        error = "not Umpire's state";
        return false;
    }
    at = 4;
    std::uint32_t version = 0, n = 0;
    const std::uint8_t* p = nullptr;
    get32(version);
    if (version < 1 || version > kVersion) {
        error = "state version " + std::to_string(version) + ", this plugin reads 1 to " + std::to_string(kVersion);
        return false;
    }
    if (version == 1) {   // the spike's one clip, from the song's start
        if (!get32(n) || !bytes(n, p)) {
            error = "the state ends inside the clip";
            return false;
        }
        if (n > 0) {
            SavedTake take;
            take.clipFile.assign(p, p + n);
            state.takes.push_back(std::move(take));
        }
    } else {
        std::uint32_t takes = 0;
        if (!get32(takes)) {
            error = "the state ends before its takes";
            return false;
        }
        for (std::uint32_t i = 0; i < takes; ++i) {
            std::uint32_t low = 0, high = 0;
            if (!get32(low) || !get32(high) || !get32(n) || !bytes(n, p)) {
                error = "the state ends inside take " + std::to_string(i + 1);
                return false;
            }
            SavedTake take;
            take.start    = static_cast<std::int64_t>(static_cast<std::uint64_t>(high) << 32 | low);
            take.clipFile.assign(p, p + n);
            if (version >= 4) {
                std::uint32_t edo = 0, reference = 0, fit = 0;
                if (!str(take.name) || !get32(edo) || !get32(reference) || !get32(fit)) {
                    error = "the state ends inside take " + std::to_string(i + 1);
                    return false;
                }
                const bool known = fit <= static_cast<std::uint32_t>(ScaleFit::Chosen);
                if (edo >= Scale::kMinEdo && edo <= Scale::kMaxEdo && reference <= 127 && known) {
                    take.scale    = Scale{static_cast<int>(edo), static_cast<int>(reference)};
                    take.scaleFit = static_cast<ScaleFit>(fit);
                }   // otherwise worked out again, as an older state's
            }
            state.takes.push_back(std::move(take));
        }
        if (version >= 4 && !get32(state.nextTakeNumber)) {
            error = "the state ends after its takes";
            return false;
        }
    }
    if (!get32(n) || !bytes(n, p)) {
        error = "the state ends inside the synth's id";
        return false;
    }
    state.synthId.assign(reinterpret_cast<const char*>(p), n);
    if (!get32(n) || !bytes(n, p)) {
        error = "the state ends inside the synth's state";
        return false;
    }
    state.synthState.resize(n);
    if (n > 0) std::memcpy(state.synthState.data(), p, n);
    if (version >= 3) {
        std::uint32_t flags = 0;
        if (!get32(flags)) {
            error = "the state ends before its flags";
            return false;
        }
        state.armed = (flags & kArmed) != 0;
        if (version >= 4) state.synthChosen = (flags & kSynthChosen) != 0;
        state.hearNotes = (flags & kHearOff) == 0;
    }
    if (version < 4) {   // the synth was named by its id only; none named, the setup file's
        state.synthChosen = !state.synthId.empty();
        return true;
    }
    std::uint32_t mode = 0;
    if (!str(state.synthPath) || !str(state.synthName) || !get32(mode) || !str(state.input.name) ||
        !str(state.input.id)) {
        error = "the state ends inside its choice of synth and input";
        return false;
    }
    state.input.mode = mode <= static_cast<std::uint32_t>(InputMode::None) ? static_cast<InputMode>(mode) : InputMode::Setup;
    if (version < 5) return true;   // no zoom: at 1
    std::uint32_t lane = 0, grid = 0;
    if (!get32(lane) || !get32(grid)) {
        error = "the state ends inside its zoom";
        return false;
    }
    // Out of the range, a factor saved by a later version that zooms further,
    // say, is brought into it; not a number, 1.
    state.zoom.lane = ui::Zoom::clamp(std::bit_cast<float>(lane));
    state.zoom.grid = ui::Zoom::clamp(std::bit_cast<float>(grid));
    if (version < 6) return true;   // no effects
    std::uint32_t effects = 0;
    if (!get32(effects)) {
        error = "the state ends before its MIDI effects";
        return false;
    }
    for (std::uint32_t i = 0; i < effects; ++i) {
        SavedEffect   e;
        std::uint32_t flags = 0;
        if (!str(e.id) || !str(e.path) || !str(e.name) || !get32(flags) || !get32(n) || !bytes(n, p)) {
            error = "the state ends inside MIDI effect " + std::to_string(i + 1);
            return false;
        }
        e.bypassed = (flags & kBypassed) != 0;
        e.state.resize(n);
        if (n > 0) std::memcpy(e.state.data(), p, n);
        state.effects.push_back(std::move(e));
    }
    return true;
}

} // namespace umpire::plugin
