#include "TestSignals.h"

#include "engine/analysis/Chroma.h"
#include "engine/analysis/tempo/BeatTracker.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace jaudios;
using namespace jaudios::analysis;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
RhythmResult analyse (const juce::AudioBuffer<float>& mono)
{
    return analyzeRhythm (mono.getReadPointer (0), mono.getNumSamples(), 44100.0);
}

/** Mean |error| (ms) between detected beats and the nearest true beat, over the true beats. */
double meanBeatErrorMs (const std::vector<double>& detected, double bpm, double offset, int numBeats)
{
    double sum = 0.0;
    for (int b = 0; b < numBeats; ++b)
    {
        const double truth = offset + b * 60.0 / bpm;
        double best = 1.0e9;
        for (double d : detected)
            best = std::min (best, std::abs (d - truth));
        sum += best;
    }
    return 1000.0 * sum / numBeats;
}
} // namespace

TEST_CASE ("Tempo of click tracks is within 2%", "[tempo]")
{
    const double bpm = GENERATE (70.0, 90.0, 100.0, 124.0, 132.0, 150.0, 172.0);
    const auto audio = test::clickTrack (bpm, 0.1, (int) (bpm / 2), false); // ~30 s

    const auto r = analyse (audio);
    INFO ("truth " << bpm << " detected " << r.bpm);
    CHECK_THAT (r.bpm, WithinRel (bpm, 0.02));
}

TEST_CASE ("Beat positions land on the clicks", "[tempo][beats]")
{
    const double bpm = 124.0, offset = 0.237;
    const int numBeats = 64;
    const auto r = analyse (test::clickTrack (bpm, offset, numBeats, false));

    CHECK (meanBeatErrorMs (r.beats, bpm, offset, numBeats) < 5.0);
    CHECK (r.beats.size() >= (size_t) numBeats - 1);
    CHECK (r.beats.size() <= (size_t) numBeats + 1);
}

TEST_CASE ("Accented downbeats set the bar phase", "[tempo][downbeat]")
{
    const double bpm = 120.0, offset = 0.5;
    const int numBeats = 64;
    // One unaccented pickup click (beat 4), then accented bars: the first downbeat is the 2nd click.
    auto audio = test::clickTrack (bpm, offset + 0.5, numBeats, true);              // accented every 4 from here
    auto pickup = test::clickTrack (bpm, offset, 1, false, 44100.0, 0.0);           // one unaccented pickup click
    for (int i = 0; i < pickup.getNumSamples(); ++i)
        audio.getWritePointer (0)[i] += pickup.getSample (0, i);

    const auto r = analyse (audio);

    REQUIRE_FALSE (r.downbeats.empty());
    CHECK_THAT (r.downbeats.front(), WithinAbs (offset + 0.5, 0.010));
    for (size_t i = 1; i < r.downbeats.size(); ++i)
        CHECK_THAT (r.downbeats[i] - r.downbeats[i - 1], WithinRel (2.0, 0.02));
}

TEST_CASE ("Harmonic changes set the bar phase under uniform drums", "[tempo][downbeat]")
{
    // 4-on-the-floor (no accents) + one chord per bar starting at bar line: Am F C G.
    const double bpm = 124.0, bar = 4 * 60.0 / bpm, offset = 0.3;
    auto notes = test::progression (0, false, { 5, 3, 0, 4 }, bar, 4); // Am F C G spelled in C major
    for (auto& n : notes)
        n.start += offset;

    auto audio = test::renderNotes (notes, offset + 16 * bar + 1.0);
    test::addFourOnFloor (audio, bpm, offset, 64);

    const auto r = analyse (audio);

    INFO ("bpm " << r.bpm);
    REQUIRE (r.downbeats.size() >= 14);
    // every detected downbeat must sit on a true bar line (within 10 ms)
    for (double d : r.downbeats)
    {
        const double barsFromStart = (d - offset) / bar;
        CHECK_THAT ((barsFromStart - std::round (barsFromStart)) * bar, WithinAbs (0.0, 0.010));
    }
}

TEST_CASE ("Beat grid is stable when the same audio starts later", "[tempo][beats]")
{
    const double bpm = 128.0;
    auto audio = test::clickTrack (bpm, 0.05, 64, true);
    test::addFourOnFloor (audio, bpm, 0.05, 64);
    const auto shifted = test::prependSilence (audio, 0.5);

    const auto a = analyse (audio);
    const auto b = analyse (shifted);

    REQUIRE (a.beats.size() == b.beats.size());
    for (size_t i = 0; i < a.beats.size(); ++i)
        CHECK_THAT (b.beats[i] - a.beats[i], WithinAbs (0.5, 0.002));
    REQUIRE_FALSE (a.downbeats.empty());
    CHECK_THAT (b.downbeats.front() - a.downbeats.front(), WithinAbs (0.5, 0.002));
}

TEST_CASE ("Fixed-tempo material gets a fitted grid", "[tempo][beats]")
{
    const auto r = analyse (test::clickTrack (124.0, 0.1, 96, true));

    CHECK (r.fixedTempo);
    CHECK (r.gridResidualMs < 3.0);
    CHECK_THAT (r.bpm, WithinAbs (124.0, 0.1)); // fit is far more precise than 2%
}

TEST_CASE ("Silence and very short input do not crash", "[tempo]")
{
    juce::AudioBuffer<float> silence (1, 44100 * 3);
    silence.clear();
    const auto a = analyse (silence);
    CHECK (a.bpm == 0.0);
    CHECK (a.beats.empty());
    CHECK (a.downbeats.empty());

    juce::AudioBuffer<float> tiny (1, 100);
    tiny.clear();
    const auto b = analyse (tiny);
    CHECK (b.beats.empty());
}

TEST_CASE ("Per-bar chroma follows the chord of each bar", "[tempo][chroma]")
{
    const double bpm = 120.0, bar = 2.0;
    // Am | F | C | G  repeated: check chroma argmax set per bar
    auto audio = test::renderNotes (test::progression (0, false, { 5, 3, 0, 4 }, bar, 3), 12 * bar + 0.5);
    test::addFourOnFloor (audio, bpm, 0.0, 48);

    const auto r = analyse (audio);
    REQUIRE (r.bars.size() >= 11);

    // Am {9,0,4}, F {5,9,0}, C {0,4,7}, G {7,11,2}: the strongest chroma bin must be a chord tone
    const int roots[] = { 9, 5, 0, 7 };
    int correct = 0, checked = 0;
    for (const auto& b : r.bars)
    {
        const double mid = 0.5 * (b.startSec + b.endSec);
        const int chordIndex = ((int) std::floor (mid / bar)) % 4;
        const int top = (int) (std::max_element (b.chroma.begin(), b.chroma.end()) - b.chroma.begin());
        const int root = roots[chordIndex];
        const int third = chordIndex == 0 ? 3 : 4; // Am is minor, F/C/G are major
        const bool inChord = top == root || top == (root + third) % 12 || top == (root + 7) % 12;
        correct += inChord ? 1 : 0;
        ++checked;
    }
    CHECK (correct == checked);
}
