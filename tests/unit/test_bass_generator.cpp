// bass-generator: patterns, pitch rules, groove, sidechain hints (no audio, no models).
#include "engine/generators/BassGenerator.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <set>

using namespace jaudios;
using namespace jaudios::generators;
using Catch::Matchers::WithinAbs;
using theory::Chord;
using theory::Key;
using theory::Mode;
using theory::Quality;

namespace
{
/** One chord per 2 s bar (120 BPM), `repeats` times. */
BassInput input (Key key, const std::vector<Chord>& chords, int repeats = 2)
{
    BassInput in;
    in.key = key;
    int b = 0;
    for (int r = 0; r < repeats; ++r)
        for (const auto& c : chords)
        {
            const double t0 = 2.0 * b++;
            in.bars.push_back ({ t0, t0 + 2.0 });
            in.progression.slots.push_back ({ t0, t0 + 2.0, b - 1, 1 });
            in.progression.chords.push_back (c);
        }
    return in;
}

const Key C { 0, Mode::major }, Am { 9, Mode::minor }, Fm { 5, Mode::minor };
const std::vector<Chord> pop { { 0, Quality::maj }, { 7, Quality::maj }, { 9, Quality::min }, { 5, Quality::maj } };
const std::vector<Chord> borrowed { { 0, Quality::maj }, { 10, Quality::maj }, { 5, Quality::maj }, { 0, Quality::maj } }; // I bVII IV I
const std::vector<Chord> minor { { 9, Quality::min }, { 5, Quality::maj }, { 0, Quality::maj }, { 7, Quality::maj } };
const std::vector<Chord> techno { { 5, Quality::min }, { 5, Quality::min }, { 10, Quality::min }, { 5, Quality::min } };

const Chord& chordAt (const BassInput& in, double t)
{
    for (size_t i = 0; i < in.progression.slots.size(); ++i)
        if (t >= in.progression.slots[i].startSec - 1e-9 && t < in.progression.slots[i].endSec)
            return in.progression.chords[i];
    return in.progression.chords.back();
}
} // namespace

TEST_CASE ("Built-in bass patterns load, cover every style and round-trip", "[bass][patterns]")
{
    const auto& all = builtinBassPatterns();
    REQUIRE (all.size() >= 8);
    std::set<std::string> names;
    for (const auto& p : all)
    {
        INFO (p.name);
        CHECK (names.insert (p.name).second);
        juce::String err;
        const auto back = BassPattern::fromJson (p.toJson(), err);
        REQUIRE (back.has_value());
        CHECK (back->steps.size() == p.steps.size());
        CHECK (back->grid == p.grid);
    }
    for (int s = 0; s < numStyles; ++s)
        CHECK_FALSE (bassPatternsFor ((Style) s).empty());
    REQUIRE (findBassPattern ("house_offbeat") != nullptr);
    CHECK (findBassPattern ("house_offbeat")->style == "house");
    CHECK (findBassPattern ("nope") == nullptr);
}

TEST_CASE ("Invalid patterns are rejected with a reason", "[bass][patterns]")
{
    auto parse = [] (const char* json) {
        juce::String err;
        const auto p = BassPattern::fromJson (juce::JSON::parse (json), err);
        return p ? juce::String() : err;
    };
    CHECK (parse (R"({"name":"a","style":"pop","grid":8,"bars":1,"steps":[{"step":0,"role":"root"}]})").isEmpty());
    CHECK (parse (R"({"name":"a","style":"pop","grid":12,"bars":1,"steps":[{"step":0,"role":"root"}]})").contains ("grid"));
    CHECK (parse (R"({"name":"a","style":"jazz","grid":8,"bars":1,"steps":[{"step":0,"role":"root"}]})").contains ("style"));
    CHECK (parse (R"({"name":"a","style":"pop","grid":8,"bars":1,"steps":[{"step":8,"role":"root"}]})").contains ("outside"));
    CHECK (parse (R"({"name":"a","style":"pop","grid":8,"bars":1,"steps":[{"step":0,"length":3,"role":"root"},{"step":2,"role":"root"}]})").contains ("overlap"));
    CHECK (parse (R"({"name":"a","style":"pop","grid":8,"bars":1,"steps":[{"step":0,"role":"bend"}]})").contains ("role"));
    CHECK (parse (R"({"name":"a","style":"pop","grid":8,"bars":1,"steps":[]})").contains ("steps"));
    CHECK (parse (R"({"name":"a","style":"pop","grid":8,"bars":1,"steps":[{"step":0,"role":"root","color":1}]})").contains ("color"));
}

TEST_CASE ("Skill acceptance: every bass note is a chord tone or a scale tone", "[bass][acceptance]")
{
    struct Case { Key key; const std::vector<Chord>* chords; const char* name; };
    for (const auto& c : { Case { C, &pop, "C pop" }, Case { C, &borrowed, "C I-bVII-IV-I" }, Case { Am, &minor, "Am i-VI-III-VII" },
                           Case { Fm, &techno, "Fm techno" } })
        for (const auto& pattern : builtinBassPatterns())
        {
            const auto in = input (c.key, *c.chords);
            const auto notes = generateBass (in, pattern);
            INFO (c.name << " / " << pattern.name);
            REQUIRE_FALSE (notes.empty());
            const auto scale = theory::scalePitchClasses (c.key);
            for (const auto& n : notes)
            {
                const auto& chord = chordAt (in, n.startSec + 1e-6);
                const bool scaleTone = std::find (scale.begin(), scale.end(), n.pitch % 12) != scale.end();
                INFO ("pitch " << n.pitch << " at " << n.startSec << " over " << chord.name (c.key) << " (" << roleName (n.role) << ")");
                CHECK ((chord.contains (n.pitch % 12) || scaleTone));
                CHECK (n.pitch >= 28);
                CHECK (n.pitch <= 52);
            }
        }
}

TEST_CASE ("Root on every chord's downbeat; octave jumps <= 20 %", "[bass]")
{
    for (const auto& pattern : builtinBassPatterns())
    {
        const auto in = input (C, pop, 4);
        const auto notes = generateBass (in, pattern);
        INFO (pattern.name);
        int jumps = 0;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            for (const auto& s : in.progression.slots)
                if (std::abs (notes[i].startSec - s.startSec) < 1e-6)
                    CHECK (notes[i].pitch % 12 == chordAt (in, s.startSec).root);
            if (i > 0 && std::abs (notes[i].pitch - notes[i - 1].pitch) >= 12)
                ++jumps;
        }
        CHECK ((double) jumps <= 0.2 * (double) (notes.size() - 1) + 1e-9);
    }
}

TEST_CASE ("Timing: grid, loop length, swing and gate", "[bass][groove]")
{
    const auto in = input (C, pop, 1);
    const auto* house = findBassPattern ("house_offbeat");
    REQUIRE (house != nullptr);
    const auto notes = generateBass (in, *house);
    REQUIRE (notes.size() == 16); // 4 offbeats x 4 bars
    CHECK_THAT (notes[0].startSec, WithinAbs (0.25, 1e-9));  // step 2 of 16 in a 2 s bar
    CHECK_THAT (notes[0].endSec, WithinAbs (0.25 + 0.25 * 0.8, 1e-9)); // length 2 steps, gate 0.8

    BassPattern swung = *house;
    swung.steps = { { 1, 1, 100, 1.0, BassRole::root }, { 2, 1, 100, 1.0, BassRole::root } };
    swung.swing = 0.5;
    const auto s = generateBass (in, swung);
    CHECK_THAT (s[0].startSec, WithinAbs (0.125 + 0.5 * 0.125, 1e-9)); // odd step delayed half a step
    CHECK_THAT (s[1].startSec, WithinAbs (0.25, 1e-9));

    // re-quantised to the 1/8 grid: 16th steps 1 and 2 both land on 8th step 1 and merge into one note
    BassSettings coarse;
    coarse.gridOverride = 8;
    coarse.swingOverride = 0.0;
    const auto c8 = generateBass (in, swung, coarse);
    REQUIRE (c8.size() == 4); // one per bar
    CHECK_THAT (c8[0].startSec, WithinAbs (0.25, 1e-9));

    // a 2-bar loop repeats every 2 bars
    const auto* funk = findBassPattern ("funk_syncopated");
    REQUIRE (funk != nullptr);
    REQUIRE (funk->bars == 2);
    const auto f = generateBass (input (C, pop, 1), *funk);
    CHECK (f.size() == 2 * funk->steps.size());
}

TEST_CASE ("Sidechain hints mark notes that meet a kick", "[bass][sidechain]")
{
    auto in = input (C, pop, 1);
    for (int beat = 0; beat < 16; ++beat)
        in.kicks.push_back (0.5 * beat); // four on the floor
    const auto offbeat = generateBass (in, *findBassPattern ("house_offbeat"));
    for (const auto& n : offbeat)
        CHECK_FALSE (n.sidechain); // the offbeat bass sits between the kicks
    const auto held = generateBass (in, *findBassPattern ("pop_root"));
    REQUIRE_FALSE (held.empty());
    CHECK (held[0].sidechain);    // starts on the kick
}

TEST_CASE ("Low vocal notes push a clashing bass note out of the way", "[bass]")
{
    auto in = input (C, { Chord { 0, Quality::maj } }, 1);
    BassPattern hold { "hold", "pop", "", 8, 1, 0.0, { { 0, 8, 100, 1.0, BassRole::root } } };
    BassSettings s;
    s.lowestMidi = 36;
    s.highestMidi = 52;
    const auto alone = generateBass (in, hold, s);
    REQUIRE (alone.size() == 1);
    const int bass = alone[0].pitch; // a C in the register
    in.vocals.push_back ({ 0.0, 2.0, bass - 2 + 12 > 51 ? bass - 2 : bass + 2, 0.9 }); // a second away, below E3
    const auto moved = generateBass (in, hold, s);
    REQUIRE (moved.size() == 1);
    CHECK (moved[0].pitch % 12 == 0);
    for (const auto& v : in.vocals)
        CHECK (std::abs (moved[0].pitch - v.pitch) > 4);
}

TEST_CASE ("Bass generation is deterministic", "[bass]")
{
    const auto in = input (Am, minor, 2);
    for (const auto& pattern : builtinBassPatterns())
    {
        const auto a = generateBass (in, pattern), b = generateBass (in, pattern);
        REQUIRE (a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i)
            CHECK (a[i].pitch == b[i].pitch);
    }
}

//==============================================================================
#include "engine/analysis/tempo/KickDetector.h"

#include <random>

namespace
{
/** Synthetic drums: kicks (pitch-swept sine, like the fixture renderer's) + bright hats between. */
std::vector<float> drums (const std::vector<double>& kickTimes, double hatEvery, double seconds, double sr = 44100.0)
{
    std::vector<float> x ((size_t) (seconds * sr), 0.0f);
    for (double t0 : kickTimes)
    {
        double phase = 0.0;
        for (int i = 0; i < (int) (0.35 * sr); ++i)
        {
            const double t = i / sr;
            phase += 2.0 * 3.14159265358979 * (48.0 + 110.0 * std::exp (-t / 0.03)) / sr;
            const auto idx = (size_t) (t0 * sr) + (size_t) i;
            if (idx < x.size())
                x[idx] += (float) (0.8 * std::sin (phase) * std::exp (-t / 0.12));
        }
    }
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    for (double t0 = hatEvery / 2; t0 < seconds; t0 += hatEvery)
    {
        float prev = 0.0f;
        for (int i = 0; i < (int) (0.05 * sr); ++i)
        {
            const auto idx = (size_t) (t0 * sr) + (size_t) i;
            const float w = u (rng);
            if (idx < x.size())
                x[idx] += 0.3f * (w - prev) * (float) std::exp (-i / (0.012 * sr));
            prev = w;
        }
    }
    return x;
}
} // namespace

TEST_CASE ("Kick detector finds kicks, not hats", "[bass][kicks]")
{
    std::vector<double> truth;
    for (int b = 0; b < 16; ++b)
        if (b % 4 != 3) // a gap every bar: no metronomic help
            truth.push_back (0.25 + 0.5 * b);
    const auto audio = drums (truth, 0.25, 8.5);
    const auto kicks = analysis::detectKicks (audio.data(), (int) audio.size(), 44100.0);
    INFO (kicks.size() << " kicks found, " << truth.size() << " expected");
    REQUIRE (kicks.size() == truth.size());
    for (size_t i = 0; i < truth.size(); ++i)
        CHECK (std::abs (kicks[i] - truth[i]) < 0.02);
}

TEST_CASE ("Kick detector: silence and hats only give no kicks", "[bass][kicks]")
{
    std::vector<float> silence (44100, 0.0f);
    CHECK (analysis::detectKicks (silence.data(), (int) silence.size(), 44100.0).empty());
    const auto hats = drums ({}, 0.125, 4.0);
    CHECK (analysis::detectKicks (hats.data(), (int) hats.size(), 44100.0).empty());
}
