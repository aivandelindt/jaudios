// chord-engine: progression suggestions on synthetic evidence (no audio, no models).
#include "engine/generators/ChordEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <set>

using namespace jaudios;
using namespace jaudios::generators;
using theory::Chord;
using theory::Key;
using theory::Mode;
using theory::Quality;

namespace
{
/** Evidence for a progression: per bar, the chord tones as melody notes (arpeggio), the root as
    bass, and a matching chroma; 2 s bars. */
HarmonicSource evidence (Key key, const std::vector<Chord>& chords, int repeats = 2, double margin = 0.6)
{
    HarmonicSource src;
    src.key = key;
    src.keyMargin = margin;
    const double bar = 2.0;
    int b = 0;
    for (int r = 0; r < repeats; ++r)
        for (const auto& c : chords)
        {
            const double t0 = b * bar;
            BarEvidence e { t0, t0 + bar, std::array<float, 12> {} };
            const auto pcs = c.pitchClasses();
            for (size_t k = 0; k < pcs.size(); ++k)
            {
                (*e.chroma)[(size_t) pcs[k]] = k == 0 ? 1.0f : 0.8f;
                src.melody.push_back ({ t0 + 0.5 * (double) k, t0 + 0.5 * (double) k + 0.45, 60 + (pcs[k] + 12 - 0) % 12, 0.9 });
            }
            src.bass.push_back ({ t0, t0 + bar * 0.9, 36 + c.bassPitchClass(), 0.9 });
            src.bars.push_back (e);
            ++b;
        }
    return src;
}

const Key C { 0, Mode::major }, Am { 9, Mode::minor };
const Chord I { 0, Quality::maj }, IV { 5, Quality::maj }, V { 7, Quality::maj }, vi { 9, Quality::min };
} // namespace

TEST_CASE ("Every style has parseable templates in both modes, borrowed chords included", "[chords][engine]")
{
    bool borrowedSeen = false;
    for (int s = 0; s < numStyles; ++s)
        for (auto mode : { Mode::major, Mode::minor })
        {
            const auto& ts = templates ((Style) s, mode);
            INFO (styleName ((Style) s) << (mode == Mode::major ? " major" : " minor"));
            REQUIRE (ts.size() >= 3);
            for (const auto& t : ts)
            {
                CHECK (t.weight > 0.0);
                CHECK (t.weight <= 1.0);
                REQUIRE (! t.romans.empty());
                for (const auto& r : t.romans)
                {
                    const auto d = theory::parseRoman (r, mode);
                    INFO (t.name << ": " << r);
                    REQUIRE (d.has_value());
                    borrowedSeen = borrowedSeen || ! theory::isDiatonic (Key { 0, mode }, theory::chordOf (Key { 0, mode }, *d));
                }
            }
        }
    CHECK (borrowedSeen); // bVI / bVII / iv in major (skill: modal mixture)
    CHECK (styleFromName ("lo-fi") == Style::lofi);
    CHECK (styleFromName ("R&B") == Style::rnb);
    CHECK_FALSE (styleFromName ("polka").has_value());
}

TEST_CASE ("Slots follow the harmonic rhythm on the bars", "[chords][engine]")
{
    const auto src = evidence (C, { I, V, vi, IV }, 1, 1.0); // bars 0..8 s
    const auto one = makeSlots (src.bars, HarmonicRhythm::onePerBar);
    const auto two = makeSlots (src.bars, HarmonicRhythm::twoPerBar);
    const auto half = makeSlots (src.bars, HarmonicRhythm::onePerTwoBars);
    REQUIRE (one.size() == 4);
    REQUIRE (two.size() == 8);
    REQUIRE (half.size() == 2);
    CHECK (two[1].startSec == 1.0);
    CHECK (half[1].startSec == 4.0);
    CHECK (half[1].endSec == 8.0);
    CHECK (half[1].numBars == 2);
}

TEST_CASE ("C major source: top-1 is I V vi IV with small voice leading", "[chords][engine][acceptance]")
{
    const auto src = evidence (C, { I, V, vi, IV });
    const auto out = suggest (src);
    REQUIRE (out.size() == 8);
    const auto& top = out.front();
    REQUIRE (top.chords.size() == 8);
    for (size_t i = 0; i < 8; ++i)
        CHECK (top.chords[i].root == std::vector<int> { 0, 7, 9, 5 }[i % 4]);
    std::set<std::string> allowed { "I", "IV", "V", "vi" };
    for (const auto& r : top.romans (C))
        CHECK (allowed.count (r) == 1); // skill acceptance
    CHECK (top.maxVoiceLeading <= 6);   // skill acceptance
    CHECK (top.summary (C) == "I V vi IV");

    // best first, all distinct
    for (size_t i = 1; i < out.size(); ++i)
    {
        CHECK (out[i - 1].score.total >= out[i].score.total);
        CHECK (out[i - 1].chords != out[i].chords);
    }
}

TEST_CASE ("A minor source: i VI III VII", "[chords][engine]")
{
    const auto src = evidence (Am, { Chord { 9, Quality::min }, Chord { 5, Quality::maj }, Chord { 0, Quality::maj }, Chord { 7, Quality::maj } });
    const auto top = suggest (src).front();
    CHECK (top.summary (Am) == "i VI III VII");
}

TEST_CASE ("Bass in the third gives a first inversion", "[chords][engine]")
{
    auto src = evidence (C, { I, V, vi, IV });
    for (auto& b : src.bass)
        if (b.startSec < 2.0 || (b.startSec >= 8.0 && b.startSec < 10.0))
            b.pitch = 36 + 4; // E under the C bars
    const auto top = suggest (src).front();
    CHECK (top.chords[0] == Chord { 0, Quality::maj, 1 });
    CHECK (top.chords[0].name (C) == "C/E");
}

TEST_CASE ("Low key confidence down-weights the template prior", "[chords][engine]")
{
    const auto sure = suggest (evidence (C, { I, V, vi, IV }, 2, 0.6)).front();
    const auto unsure = suggest (evidence (C, { I, V, vi, IV }, 2, 0.02)).front();
    CHECK (unsure.chords == sure.chords); // the evidence still decides
    CHECK (unsure.score.prior < sure.score.prior);
}

TEST_CASE ("Deterministic given the seed; two chords per bar", "[chords][engine]")
{
    const auto src = evidence (C, { I, V, vi, IV });
    const auto a = suggest (src), b = suggest (src);
    REQUIRE (a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i)
        CHECK (a[i].chords == b[i].chords);

    EngineSettings two;
    two.rhythm = HarmonicRhythm::twoPerBar;
    const auto t = suggest (src, two).front();
    CHECK (t.chords.size() == 16);
}

TEST_CASE ("Alternatives: six per slot, ranked, without the current chord", "[chords][engine]")
{
    const auto src = evidence (C, { I, V, vi, IV });
    const auto top = suggest (src).front();
    const auto alts = alternatives (src, top, 2);
    REQUIRE (alts.size() == 6);
    for (size_t i = 0; i < alts.size(); ++i)
    {
        CHECK_FALSE (alts[i].chord == top.chords[2]);
        if (i > 0)
            CHECK (alts[i - 1].score >= alts[i].score);
    }
    // the substitutes that keep the melody consonant come first: C (I) or Em (iii) share two tones with Am
    CHECK ((alts[0].roman == "I" || alts[0].roman == "iii" || alts[0].roman == "IV"));
}

TEST_CASE ("Render: block chords per slot, voice-led, humanized deterministically", "[chords][engine][render]")
{
    const auto top = suggest (evidence (C, { I, V, vi, IV }, 1)).front();
    RenderSettings rs;
    rs.humanize = 8;
    rs.seed = 7;
    const auto notes = render (top, rs);
    const auto again = render (top, rs);
    REQUIRE (notes.size() == again.size());
    for (size_t i = 0; i < notes.size(); ++i)
        CHECK (notes[i].velocity == again[i].velocity);

    for (int slot = 0; slot < 4; ++slot)
    {
        std::vector<int> pitches;
        for (const auto& n : notes)
            if (n.slot == slot)
            {
                pitches.push_back (n.pitch);
                CHECK (std::abs (n.velocity - rs.velocity) <= 8);
                CHECK (top.chords[(size_t) slot].contains (n.pitch % 12));
            }
        REQUIRE (pitches.size() == 4); // bass + 3 upper voices
        CHECK (*std::min_element (pitches.begin(), pitches.end()) % 12 == top.chords[(size_t) slot].bassPitchClass());
    }
}

TEST_CASE ("Key / mode change re-renders in < 50 ms", "[chords][engine][timing]")
{
    const auto src = evidence (C, { I, V, vi, IV }, 8); // 32 bars
    double worstMs = 0.0;
    for (int k = 0; k < 24; ++k)
    {
        auto s = src;
        s.key = theory::keyFromIndex24 (k);
        const auto t0 = std::chrono::steady_clock::now();
        const auto out = suggest (s);
        const auto t1 = std::chrono::steady_clock::now();
        REQUIRE_FALSE (out.empty());
        worstMs = std::max (worstMs, std::chrono::duration<double, std::milli> (t1 - t0).count());
    }
    WARN ("worst suggest() over 24 keys, 32 bars: " << worstMs << " ms");
   #if ! JUCE_DEBUG && ! defined(__SANITIZE_ADDRESS__)
    CHECK (worstMs < 50.0);
   #endif
}
