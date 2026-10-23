// Working out a clip's scale from its notes (src/App/Scale.h): pitches as
// KeyboardEDO sends them, Pitch 7.9 rounded to 1/512 of a semitone. Silent.

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "App/Scale.h"

namespace {

int failures = 0;

void check(const char* what, bool ok, const std::string& detail = {}) {
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str());
    if (!ok) ++failures;
}

// Steps of `edo` anchored at MIDI note `anchor`, as KeyboardEDO's Pitch 7.9.
std::vector<std::uint16_t> steps(int edo, int anchor, const std::vector<int>& which) {
    std::vector<std::uint16_t> out;
    for (int s : which) out.push_back(static_cast<std::uint16_t>(std::lround((anchor + s * 12.0 / edo) * 512)));
    return out;
}

std::string describe(const umpire::ScaleGuess& g) {
    std::string s = std::to_string(g.scale.edo) + "-EDO from " + std::to_string(g.scale.referenceNote) + ", fit " +
                    std::to_string(static_cast<int>(g.fit)) + ", candidates";
    for (int c : g.candidates) s += " " + std::to_string(c);
    return s;
}

} // namespace

int main() {
    using namespace umpire;
    const std::vector<int> melody{0, 3, 5, 8, 10, 13, 18, 21, 26, 31, 34};

    {
        const auto g = workOutScale({});
        check("no notes: 12-EDO from 60, by default", g.scale == Scale{} && g.fit == ScaleFit::Default);
    }
    {
        const auto g = workOutScale(steps(12, 60, {0, 2, 4, 5, 7, 9, 11, 12}));
        check("a 12-EDO tune", g.scale == (Scale{12, 60}) && g.fit == ScaleFit::WorkedOut, describe(g));
    }
    {
        const auto g = workOutScale(steps(12, 60, {0, 2, 4, 6, 8}));
        check("a whole-tone tune is 12-EDO, not 6", g.scale.edo == 12 && g.fit == ScaleFit::WorkedOut, describe(g));
    }
    {
        const auto g = workOutScale(steps(31, 57, melody));   // KeyboardEDO's default anchor, A
        check("31-EDO from A: the As are on the lattice, 57 nearest 60",
              g.scale == (Scale{31, 57}) && g.fit == ScaleFit::WorkedOut, describe(g));
    }
    {
        const auto g = workOutScale(steps(31, 52, melody));   // anchored at E
        check("31-EDO from E: the Es", g.scale == (Scale{31, 64}) && g.fit == ScaleFit::WorkedOut, describe(g));
    }
    {
        const auto g = workOutScale(steps(19, 60, melody));
        check("19-EDO from C", g.scale == (Scale{19, 60}) && g.fit == ScaleFit::WorkedOut, describe(g));
    }
    {
        const auto g = workOutScale(steps(24, 57, {1, 3, 7, 11}));   // quarter tones only
        check("24-EDO: any note is on the lattice, so 60", g.scale == (Scale{24, 60}) && g.fit == ScaleFit::WorkedOut,
              describe(g));
    }
    {
        const auto g = workOutScale(steps(53, 62, melody));
        check("53-EDO", g.scale.edo == 53 && g.fit == ScaleFit::WorkedOut, describe(g));
    }
    {
        const auto g = workOutScale(steps(31, 57, {0, 31, 62}));   // only octaves of a 12-EDO note
        check("only As: 12-EDO", g.scale.edo == 12, describe(g));
    }
    {
        const auto g = workOutScale(steps(31, 57, {10}));   // one note, off 12-EDO
        check("one note off 12-EDO: unsure, several EDOs fit", g.fit == ScaleFit::Unsure && g.candidates.size() > 1,
              describe(g));
    }
    {
        // A tune bent by ear, on no lattice.
        const auto g = workOutScale({static_cast<std::uint16_t>(60.11 * 512), static_cast<std::uint16_t>(62.37 * 512),
                                     static_cast<std::uint16_t>(63.91 * 512), static_cast<std::uint16_t>(65.23 * 512),
                                     static_cast<std::uint16_t>(67.06 * 512), static_cast<std::uint16_t>(68.71 * 512)});
        check("pitches on no EDO: no fit, 12-EDO from 60", g.fit == ScaleFit::NoFit && g.scale == Scale{}, describe(g));
    }
    {
        // Rounding as a sender that truncates instead of rounding: still found.
        std::vector<std::uint16_t> truncated;
        for (int s : melody) truncated.push_back(static_cast<std::uint16_t>((57 + s * 12.0 / 31) * 512));
        const auto g = workOutScale(truncated);
        check("31-EDO, truncated to 7.9", g.scale == (Scale{31, 57}) && g.fit == ScaleFit::WorkedOut, describe(g));
    }

    std::printf("%s: %d failure%s\n", failures == 0 ? "OK" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
