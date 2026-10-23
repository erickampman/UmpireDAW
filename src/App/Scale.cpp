#include "App/Scale.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace umpire {

namespace {

// A pitch is within half a unit of its true value (7.9 rounds to 1/512 of a
// semitone), so a distance between two is within one; a little more for
// senders that truncate.
constexpr double kTolerance = 1.5;   // in 1/512 semitone

// Whether `distance` (in 1/512 semitone) is a whole number of `step`s.
bool whole(double distance, double step) {
    const double steps = distance / step;
    return std::abs(steps - std::round(steps)) * step <= kTolerance;
}

// The MIDI note on the lattice through `pitch` with `edo` steps nearest 60, if
// the lattice meets one.
std::optional<int> reference(std::uint16_t pitch, int edo) {
    const double step = 12 * 512.0 / edo;
    std::optional<int> best;
    for (int note = 0; note < 128; ++note)
        if (whole(note * 512.0 - pitch, step) && (!best || std::abs(note - 60) < std::abs(*best - 60))) best = note;
    return best;
}

} // namespace

ScaleGuess workOutScale(const std::vector<std::uint16_t>& pitches79) {
    std::vector<std::uint16_t> pitches = pitches79;
    std::sort(pitches.begin(), pitches.end());
    pitches.erase(std::unique(pitches.begin(), pitches.end()), pitches.end());
    ScaleGuess guess;
    if (pitches.empty()) return guess;

    // Every EDO that fits, with its reference.
    std::vector<std::pair<int, int>> fits;
    for (int edo = Scale::kMinEdo; edo <= Scale::kMaxEdo; ++edo) {
        const double step = 12 * 512.0 / edo;
        const bool   all  = std::all_of(pitches.begin(), pitches.end(),
                                        [&](std::uint16_t p) { return whole(double(p) - pitches.front(), step); });
        if (!all) continue;
        if (const auto ref = reference(pitches.front(), edo)) fits.emplace_back(edo, *ref);
    }

    const auto twelve = std::find_if(fits.begin(), fits.end(), [](const auto& f) { return f.first == 12; });
    if (twelve != fits.end()) {
        guess.scale      = Scale{12, 60};
        guess.fit        = ScaleFit::WorkedOut;
        guess.candidates = {12};
        return guess;
    }
    // The EDOs that fit, leaving out multiples of a smaller one (62 when 31 fits).
    for (const auto& [edo, ref] : fits)
        if (std::none_of(guess.candidates.begin(), guess.candidates.end(), [edo](int c) { return edo % c == 0; }))
            guess.candidates.push_back(edo);
    if (guess.candidates.empty()) {
        guess.fit = ScaleFit::NoFit;
        return guess;
    }
    const auto chosen = std::find_if(fits.begin(), fits.end(), [&](const auto& f) { return f.first == guess.candidates.front(); });
    guess.scale = Scale{chosen->first, chosen->second};
    guess.fit   = guess.candidates.size() == 1 ? ScaleFit::WorkedOut : ScaleFit::Unsure;
    return guess;
}

} // namespace umpire
