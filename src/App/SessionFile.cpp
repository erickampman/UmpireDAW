#include "App/SessionFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <stdexcept>

#include "App/ClipFile.h"

namespace umpire {

namespace fs = std::filesystem;
using Json   = nlohmann::ordered_json;   // keeps fields in the order written

namespace {

constexpr int         kFormatVersion = 1;
constexpr const char* kFormatName    = "umpire-session";
constexpr const char* kSessionJson   = "session.json";

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

bool readBytes(const fs::path& path, std::vector<std::uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

template <typename Bytes>
void writeBytes(const fs::path& path, const Bytes& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) fail("cannot write " + path.string());
}

// Replace the bundle `bundle` with the complete one at `temp`: until the last
// rename the old bundle is untouched (or, briefly, "<bundle>.old"), as
// StageHand's swapInBundle.
void swapInBundle(const fs::path& temp, const fs::path& bundle) {
    const fs::path old = bundle.string() + ".old";
    fs::remove_all(old);
    if (fs::exists(bundle)) fs::rename(bundle, old);
    fs::rename(temp, bundle);
    fs::remove_all(old);
}

fs::path resolved(const std::string& text) {
    std::string expanded = text;
    if (const char* home = std::getenv("HOME"); home != nullptr && (text == "~" || text.rfind("~/", 0) == 0))
        expanded = home + text.substr(1);
    std::error_code ec;
    fs::path        path = fs::weakly_canonical(expanded, ec);
    if (ec) path = fs::absolute(expanded, ec);
    if (!path.has_filename()) path = path.parent_path();   // "X.umpire/" -> "X.umpire"
    return path;
}

const char* fitName(ScaleFit fit) {
    switch (fit) {
        case ScaleFit::Default:   return "default";
        case ScaleFit::WorkedOut: return "workedOut";
        case ScaleFit::Unsure:    return "unsure";
        case ScaleFit::NoFit:     return "noFit";
        case ScaleFit::Chosen:    return "chosen";
    }
    return "default";
}

ScaleFit parseFit(const std::string& text) {
    for (ScaleFit f : {ScaleFit::Default, ScaleFit::WorkedOut, ScaleFit::Unsure, ScaleFit::NoFit, ScaleFit::Chosen})
        if (text == fitName(f)) return f;
    fail("scaleFit '" + text + "' isn't one Umpire knows");
}

// A clip's file in the bundle: by its id, which a session never reuses.
// Two clips with one id (a scale set: App/TrackController's setClipScale
// keeps the id) have the same events, so share the file; their scales are
// in session.json.
std::string clipFileName(const Clip& clip) { return "clip-" + std::to_string(clip.id) + ".midi2"; }

std::string stateFileName(TrackId track) { return "track-" + std::to_string(track) + ".state"; }

template <typename T>
T number(const Json& j, const char* field, T low, T high) {
    if (!j.contains(field) || !j.at(field).is_number()) fail(std::string("\"") + field + "\" is missing or not a number");
    const T value = j.at(field).get<T>();
    if (!(value >= low && value <= high)) fail(std::string("\"") + field + "\" is out of range");
    return value;
}

} // namespace

// --- saving ---------------------------------------------------------------------------

SessionFileResult saveSession(const std::string& bundlePath, const SavedSession& saved) {
    SessionFileResult result;
    const fs::path    bundle = resolved(bundlePath);
    const fs::path    temp   = bundle.string() + ".tmp";
    const Session&    s      = saved.session;
    std::error_code   ec;
    try {
        fs::remove_all(temp);
        fs::create_directories(temp / "clips");

        Json doc;
        doc["format"]  = kFormatName;
        doc["version"] = kFormatVersion;
        // A list of each, ready for the tempo map (MILESTONES.md, T1); one of
        // each for now.
        doc["tempoMap"] = {{"tempos", Json::array({{{"tick", 0}, {"bpm", s.tempoBpm}}})},
                           {"meters", Json::array({{{"tick", 0}, {"beats", s.beatsPerBar}, {"unit", 4}}})}};
        doc["loop"]     = {{"on", s.looping}, {"start", s.loopStart}, {"end", s.loopEnd}};

        Json clips  = Json::array();
        Json tracks = Json::array();
        std::map<std::string, const Clip*> written;   // by file name
        const clipfile::Tempo tempo{s.tempoBpm, s.beatsPerBar};
        for (const Track& t : s.tracks) {
            Json refs = Json::array();
            for (const ClipRef& r : t.clips) {
                if (!r.clip) continue;
                const Clip&       c    = *r.clip;
                const std::string name = clipFileName(c);
                const auto [it, fresh] = written.try_emplace(name, &c);
                if (fresh) {
                    writeBytes(temp / "clips" / name, clipfile::write(c.events, c.length, tempo));
                    clips.push_back({{"id", c.id}, {"file", "clips/" + name}, {"name", c.name},
                                     {"scale", {{"edo", c.scale.edo}, {"reference", c.scale.referenceNote}}},
                                     {"scaleFit", fitName(c.scaleFit)}, {"scaleCandidates", c.scaleCandidates}});
                } else if (it->second != &c && (it->second->length != c.length ||
                                                clipfile::write(it->second->events, 0, tempo) !=
                                                    clipfile::write(c.events, 0, tempo))) {
                    fail("two different clips share id " + std::to_string(c.id));
                }
                refs.push_back({{"id", r.id}, {"clip", "clips/" + name}, {"start", r.start}});
            }
            tracks.push_back({{"id", t.id}, {"name", t.name}, {"armed", t.armed}, {"muted", t.muted},
                              {"soloed", t.soloed}, {"clips", std::move(refs)}});
        }
        doc["tracks"] = std::move(tracks);

        Json outputs = Json::array();
        for (const Output& o : s.outputs) {
            Json out = {{"track", o.track}, {"device", o.device}};
            const auto st = saved.pluginStates.find(o.track);
            if (st != saved.pluginStates.end() && st->second.device == o.device && !st->second.data.empty()) {
                fs::create_directories(temp / "plugins");
                writeBytes(temp / "plugins" / stateFileName(o.track), st->second.data);
                out["state"] = "plugins/" + stateFileName(o.track);
            }
            outputs.push_back(std::move(out));
        }
        doc["outputs"] = std::move(outputs);
        doc["clips"]   = std::move(clips);
        doc["next"]    = {{"track", saved.counters.nextTrack}, {"trackNumber", saved.counters.nextNumber},
                          {"clipRef", saved.counters.nextRef}, {"clip", saved.counters.nextClip}};
        {
            std::ofstream out(temp / kSessionJson, std::ios::trunc);
            out << doc.dump(2) << '\n';
            if (!out) fail("cannot write " + (temp / kSessionJson).string());
        }
        swapInBundle(temp, bundle);
    } catch (const std::exception& e) {   // nlohmann's and the filesystem's are std::exceptions too
        result.error = e.what();
        fs::remove_all(temp, ec);
        return result;
    }
    result.ok = true;
    return result;
}

// --- opening --------------------------------------------------------------------------

SessionFileResult loadSession(const std::string& bundlePath, SavedSession& saved) {
    SessionFileResult result;
    const fs::path    bundle   = resolved(bundlePath);
    const fs::path    jsonPath = bundle / kSessionJson;
    SavedSession      loaded;
    Session&          s = loaded.session;
    try {
        Json doc;
        {
            std::ifstream in(jsonPath);
            if (!in) fail("there's no " + std::string(kSessionJson) + " (not an Umpire session)");
            doc = Json::parse(in);
        }
        if (doc.value("format", std::string()) != kFormatName) fail("not an Umpire session");
        const int version = doc.value("version", 0);
        if (version < 1 || version > kFormatVersion)
            fail("made by a newer Umpire (format version " + std::to_string(version) + ")");

        // The tempo map's first tempo and meter: Umpire has one of each until T1.
        const Json& map = doc.at("tempoMap");
        if (!map.at("tempos").empty())
            s.tempoBpm = number<double>(map.at("tempos").at(0), "bpm", 1, 1000);
        if (!map.at("meters").empty())
            s.beatsPerBar = number<int>(map.at("meters").at(0), "beats", 1, 64);
        if (doc.contains("loop")) {
            const Json& loop = doc.at("loop");
            s.looping   = loop.value("on", false);
            s.loopStart = number<Tick>(loop, "start", 0, INT64_MAX);
            s.loopEnd   = number<Tick>(loop, "end", 0, INT64_MAX);
        }

        // The clips, by their file; each read once and shared by its references.
        std::map<std::string, std::shared_ptr<const Clip>> clips;
        ClipId highestClip = 0;
        for (const Json& c : doc.at("clips")) {
            const std::string file = c.at("file").get<std::string>();
            if (file.find("..") != std::string::npos || file.empty() || file.front() == '/')
                fail("clip file '" + file + "' isn't inside the session");
            std::vector<std::uint8_t> bytes;
            if (!readBytes(bundle / file, bytes)) fail("can't read " + file);
            clipfile::Read read = clipfile::read(bytes.data(), bytes.size());
            if (!read.ok) fail(file + ": " + read.error);
            auto clip    = std::make_shared<Clip>();
            clip->id     = number<ClipId>(c, "id", 1, UINT32_MAX);
            clip->name   = c.value("name", std::string());
            clip->length = read.length;
            clip->events = std::move(read.events);
            if (c.contains("scale")) {
                const Json& sc  = c.at("scale");
                clip->scale     = Scale{number<int>(sc, "edo", Scale::kMinEdo, Scale::kMaxEdo), number<int>(sc, "reference", 0, 127)};
                clip->scaleFit  = parseFit(c.value("scaleFit", std::string("default")));
                clip->scaleCandidates = c.value("scaleCandidates", std::vector<int>());
            } else {
                workOutScale(*clip);   // a clip file added by hand
            }
            highestClip = std::max(highestClip, clip->id);
            if (!clips.emplace(file, std::move(clip)).second) fail("clip file '" + file + "' is listed twice");
        }

        std::set<TrackId>   trackIds;
        std::set<ClipRefId> refIds;
        for (const Json& t : doc.at("tracks")) {
            Track track;
            track.id     = number<TrackId>(t, "id", 1, UINT32_MAX);
            track.name   = t.value("name", "Track " + std::to_string(track.id));
            track.armed  = t.value("armed", false);
            track.muted  = t.value("muted", false);
            track.soloed = t.value("soloed", false);
            if (!trackIds.insert(track.id).second) fail("track id " + std::to_string(track.id) + " is used twice");
            for (const Json& r : t.at("clips")) {
                ClipRef ref;
                ref.id    = number<ClipRefId>(r, "id", 1, UINT32_MAX);
                ref.start = number<Tick>(r, "start", 0, INT64_MAX);
                const std::string file = r.at("clip").get<std::string>();
                const auto        it   = clips.find(file);
                if (it == clips.end()) fail("track " + std::to_string(track.id) + " refers to a clip not listed: " + file);
                ref.clip = it->second;
                if (!refIds.insert(ref.id).second) fail("clip reference id " + std::to_string(ref.id) + " is used twice");
                track.clips.push_back(std::move(ref));
            }
            s.tracks.push_back(std::move(track));
        }

        for (const Json& o : doc.at("outputs")) {
            Output out{number<TrackId>(o, "track", 1, UINT32_MAX), o.at("device").get<std::string>()};
            if (trackIds.count(out.track) == 0 || s.outputOf(out.track) != nullptr)
                fail("an output for track " + std::to_string(out.track) + " that isn't there, or a second one");
            if (o.contains("state")) {
                const std::string file = o.at("state").get<std::string>();
                std::vector<std::uint8_t> bytes;
                if (file.find("..") != std::string::npos || !readBytes(bundle / file, bytes))
                    fail("can't read the plugin state " + file);
                PluginState& st = loaded.pluginStates[out.track];
                st.device       = out.device;
                st.data.resize(bytes.size());
                std::transform(bytes.begin(), bytes.end(), st.data.begin(), [](std::uint8_t b) { return std::byte{b}; });
            }
            s.outputs.push_back(std::move(out));
        }

        // The counters, never behind what's in the file (one edited by hand).
        const Json next = doc.value("next", Json::object());
        SessionCounters& n = loaded.counters;
        n.nextTrack  = std::max<TrackId>(next.value("track", TrackId{1}), trackIds.empty() ? 1 : *trackIds.rbegin() + 1);
        n.nextNumber = std::max(next.value("trackNumber", 1), static_cast<int>(trackIds.size()) + 1);
        n.nextRef    = std::max<ClipRefId>(next.value("clipRef", ClipRefId{1}), refIds.empty() ? 1 : *refIds.rbegin() + 1);
        n.nextClip   = std::max<ClipId>(next.value("clip", ClipId{1}), highestClip + 1);
    } catch (const std::exception& e) {
        result.error = jsonPath.string() + ": " + e.what();
        return result;
    }
    saved     = std::move(loaded);
    result.ok = true;
    return result;
}

// --- paths ------------------------------------------------------------------------------

std::string sessionBundleOf(const std::string& chosen) {
    if (chosen.empty()) return {};
    fs::path        path = resolved(chosen);
    std::error_code ec;
    if (path.filename() == kSessionJson) path = path.parent_path();
    return fs::is_regular_file(path / kSessionJson, ec) ? path.string() : std::string{};
}

std::string saveAsPath(const std::string& chosen) {
    if (chosen.empty()) return {};
    fs::path path = resolved(chosen);
    if (path.extension() != kSessionExtension) path += kSessionExtension;
    return path.string();
}

SaveTarget classifySaveTarget(const std::string& target, const std::string& current) {
    std::error_code ec;
    const fs::path  path = resolved(target);
    if (!fs::exists(path, ec)) return SaveTarget::New;
    if (!fs::is_directory(path, ec) || !fs::exists(path / kSessionJson, ec)) return SaveTarget::NotASession;
    if (!current.empty() && fs::equivalent(path, resolved(current), ec)) return SaveTarget::Current;
    return SaveTarget::OtherSession;
}

std::string sessionNameOf(const std::string& bundlePath) { return resolved(bundlePath).stem().string(); }

} // namespace umpire
