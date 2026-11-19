#include "Plugin/PluginSetup.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

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

constexpr std::uint32_t kVersion = 3;   // 1: the spike's one clip; 2: no flags

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

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
    }
    return config;
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
    }
    put32(out, static_cast<std::uint32_t>(state.synthId.size()));
    out.insert(out.end(), state.synthId.begin(), state.synthId.end());
    put32(out, static_cast<std::uint32_t>(state.synthState.size()));
    for (const std::byte b : state.synthState) out.push_back(static_cast<std::uint8_t>(b));
    put32(out, state.armed ? 1u : 0u);
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
        if (n > 0) state.takes.push_back({0, std::vector<std::uint8_t>(p, p + n)});
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
            const auto start = static_cast<std::int64_t>(static_cast<std::uint64_t>(high) << 32 | low);
            state.takes.push_back({start, std::vector<std::uint8_t>(p, p + n)});
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
        state.armed = (flags & 1u) != 0;
    }
    return true;
}

} // namespace umpire::plugin
