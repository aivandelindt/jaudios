// drum-generator: pattern library, groove generation, sampler (no audio files, no models).
#include "TestSignals.h"

#include "app/core/RealtimeTrap.h"
#include "engine/generators/DrumGenerator.h"
#include "engine/synth/DrumSampler.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <set>

using namespace jaudios;
using namespace jaudios::generators;
using Catch::Matchers::WithinAbs;

namespace
{
/** `count` bars of `barSec` seconds starting at `offset`. */
std::vector<std::pair<double, double>> barsOf (int count, double barSec = 2.0, double offset = 0.0)
{
    std::vector<std::pair<double, double>> b;
    for (int i = 0; i < count; ++i)
        b.push_back ({ offset + i * barSec, offset + (i + 1) * barSec });
    return b;
}

std::vector<DrumNote> only (const std::vector<DrumNote>& notes, DrumElement e)
{
    std::vector<DrumNote> out;
    for (const auto& n : notes)
        if (n.element == e)
            out.push_back (n);
    return out;
}

const DrumPattern& house() { return *findDrumPattern ("house_four_floor"); }
} // namespace

TEST_CASE ("Built-in drum patterns load, cover every style, have all variants and round-trip", "[drums][patterns]")
{
    const auto& all = builtinDrumPatterns();
    REQUIRE (all.size() >= 8);
    std::set<std::string> names;
    for (const auto& p : all)
    {
        INFO (p.name);
        CHECK (names.insert (p.name).second);
        CHECK (p.bars >= 2);
        CHECK (p.bars <= 8);
        for (int v = 0; v < numDrumVariants; ++v)
        {
            bool any = false;
            for (const auto& l : p.variants[(size_t) v])
                any = any || l.has_value();
            CHECK (any); // fill, riser_hit, drop and half_time in every pattern
        }
        juce::String err;
        const auto back = DrumPattern::fromJson (p.toJson(), err);
        REQUIRE (back.has_value());
        CHECK (back->lanes == p.lanes);
    }
    for (int s = 0; s < numStyles; ++s)
        CHECK_FALSE (drumPatternsFor ((Style) s).empty());
    CHECK (gmNote (DrumElement::kick) == 36);
    CHECK (variantFromName ("riser-hit") == DrumVariant::riserHit);
}

TEST_CASE ("Invalid drum patterns are rejected with a reason", "[drums][patterns]")
{
    auto parse = [] (const char* json) {
        juce::String err;
        return DrumPattern::fromJson (juce::JSON::parse (json), err) ? juce::String() : err;
    };
    const char* kick2 = "X...x...X...x...X...x...X...x...";
    CHECK (parse ((juce::String (R"({"name":"a","style":"house","bars":2,"lanes":{"kick":")") + kick2 + R"("}})").toRawUTF8()).isEmpty());
    CHECK (parse (R"({"name":"a","style":"house","bars":2,"lanes":{"kick":"X..."}})").contains ("expected 32"));
    CHECK (parse (R"({"name":"a","style":"house","bars":1,"lanes":{"kick":"X...x...X...x..."}})").contains ("bars"));
    CHECK (parse ((juce::String (R"({"name":"a","style":"house","bars":2,"lanes":{"cowbell":")") + kick2 + R"("}})").toRawUTF8()).contains ("cowbell"));
    CHECK (parse ((juce::String (R"({"name":"a","style":"house","bars":2,"lanes":{"kick":")") + kick2 + R"("},"fill":{"kick":"x..q"}})").toRawUTF8()).contains ("only X x o g"));
    CHECK (parse (R"({"name":"a","style":"house","bars":2,"lanes":{"kick":"................................"}})").contains ("no hits"));
}

TEST_CASE ("Skill acceptance: kicks on the one land exactly on the downbeats", "[drums][acceptance]")
{
    // uneven bars, as detected beat grids drift
    std::vector<std::pair<double, double>> bars { { 0.31, 2.30 }, { 2.30, 4.33 }, { 4.33, 6.31 }, { 6.31, 8.30 } };
    for (const auto& p : builtinDrumPatterns())
    {
        DrumSettings s;
        s.transitions = false;
        const auto kicks = only (generateDrums (bars, p, s), DrumElement::kick);
        INFO (p.name);
        for (size_t b = 0; b < bars.size(); ++b)
        {
            const int barInLoop = (int) b % p.bars;
            if (p.lanes[0].size() > (size_t) barInLoop * 16 && p.lanes[0][(size_t) barInLoop * 16] != '.')
            {
                const auto it = std::find_if (kicks.begin(), kicks.end(), [&] (const DrumNote& n) { return n.bar == (int) b && n.step == 0; });
                REQUIRE (it != kicks.end());
                CHECK (std::abs (it->startSec - bars[b].first) < 0.005);
                CHECK (it->startSec == bars[b].first); // exactly, in fact
            }
        }
    }
}

TEST_CASE ("Options: no kick, intensity levels, ghosts, velocity", "[drums]")
{
    const auto bars = barsOf (4);
    DrumSettings s;
    s.transitions = false;
    s.includeKick = false;
    CHECK (only (generateDrums (bars, house(), s), DrumElement::kick).empty());

    auto count = [&] (int intensity, DrumElement e) {
        DrumSettings x;
        x.transitions = false;
        x.intensity = intensity;
        return only (generateDrums (bars, house(), x), e).size();
    };
    CHECK (count (1, DrumElement::kick) == 16);
    CHECK (count (1, DrumElement::hat) == 0);
    CHECK (count (2, DrumElement::hat) > 0);
    CHECK (count (2, DrumElement::openhat) == 0);
    CHECK (count (3, DrumElement::openhat) > 0);
    CHECK (count (3, DrumElement::perc) == 0);
    CHECK (count (4, DrumElement::perc) > 0);
    CHECK (count (4, DrumElement::ride) == 0);
    CHECK (count (5, DrumElement::ride) > 0);
    CHECK (count (3, DrumElement::hat) > count (2, DrumElement::hat)); // the 'g' ghosts join at 3

    DrumSettings soft, loud;
    soft.intensity = 1;
    loud.intensity = 5;
    soft.transitions = loud.transitions = false;
    CHECK (only (generateDrums (bars, house(), soft), DrumElement::kick)[0].velocity
           < only (generateDrums (bars, house(), loud), DrumElement::kick)[0].velocity);
}

TEST_CASE ("Transitions replace the last bar of each phrase", "[drums][transitions]")
{
    const auto bars = barsOf (16);
    DrumSettings s;
    s.phraseBars = 8;

    SECTION ("fill")
    {
        const auto claps = only (generateDrums (bars, house(), s), DrumElement::clap);
        int inBar7 = 0;
        for (const auto& c : claps)
            inBar7 += c.bar == 7 ? 1 : 0;
        CHECK (inBar7 == 5); // fill clap "....X.......XoXX"
    }
    SECTION ("drop: no kick in the bar before the drop")
    {
        s.transition = DrumVariant::drop;
        const auto kicks = only (generateDrums (bars, house(), s), DrumElement::kick);
        for (const auto& k : kicks)
            CHECK (k.bar != 7);
        CHECK (std::any_of (kicks.begin(), kicks.end(), [] (const DrumNote& k) { return k.bar == 8 && k.step == 0; }));
    }
    SECTION ("riser-hit: roll, then an accented kick + open hat on the next downbeat")
    {
        s.transition = DrumVariant::riserHit;
        const auto notes = generateDrums (bars, house(), s);
        const auto kicks = only (notes, DrumElement::kick);
        const auto hit = std::find_if (kicks.begin(), kicks.end(), [] (const DrumNote& k) { return k.bar == 8 && k.step == 0; });
        REQUIRE (hit != kicks.end());
        CHECK (hit->velocity == 127);
        const auto opens = only (notes, DrumElement::openhat);
        CHECK (std::any_of (opens.begin(), opens.end(), [] (const DrumNote& o) { return o.bar == 8 && o.step == 0; }));
        CHECK (std::count_if (kicks.begin(), kicks.end(), [] (const DrumNote& k) { return k.bar == 8 && k.step == 0; }) == 1);
    }
    SECTION ("half-time: snare / clap on 3 only")
    {
        s.transition = DrumVariant::halfTime;
        const auto claps = only (generateDrums (bars, house(), s), DrumElement::clap);
        std::vector<int> steps;
        for (const auto& c : claps)
            if (c.bar == 7)
                steps.push_back (c.step);
        CHECK (steps == std::vector<int> { 8 });
    }
    SECTION ("off")
    {
        s.transitions = false;
        const auto claps = only (generateDrums (bars, house(), s), DrumElement::clap);
        int inBar7 = 0;
        for (const auto& c : claps)
            inBar7 += c.bar == 7 ? 1 : 0;
        CHECK (inBar7 == 2);
    }
}

TEST_CASE ("Swing delays odd 16ths; steps follow each bar's length", "[drums][groove]")
{
    DrumSettings s;
    s.transitions = false;
    s.swingOverride = 0.5;
    s.intensity = 2;
    const std::vector<std::pair<double, double>> bars { { 0.0, 2.0 }, { 2.0, 4.4 } };
    const auto hats = only (generateDrums (bars, house(), s), DrumElement::hat);
    // house hat lane "x.o.x.o.x.o.x.og": even steps only (g on 15 needs intensity 3)
    CHECK_THAT (hats[1].startSec, WithinAbs (2 * 0.125, 1e-9));
    const auto odd = std::find_if (hats.begin(), hats.end(), [] (const DrumNote& n) { return n.step % 2 == 1; });
    CHECK (odd == hats.end());
    // second bar is 2.4 s: step 4 at 2.0 + 4 * 0.15
    const auto it = std::find_if (hats.begin(), hats.end(), [] (const DrumNote& n) { return n.bar == 1 && n.step == 4; });
    REQUIRE (it != hats.end());
    CHECK_THAT (it->startSec, WithinAbs (2.6, 1e-9));

    s.intensity = 3; // ghost on step 15 (odd): swung by half a step
    const auto ghosts = only (generateDrums (bars, house(), s), DrumElement::hat);
    const auto g = std::find_if (ghosts.begin(), ghosts.end(), [] (const DrumNote& n) { return n.bar == 0 && n.step == 15; });
    REQUIRE (g != ghosts.end());
    CHECK_THAT (g->startSec, WithinAbs (15 * 0.125 + 0.5 * 0.125, 1e-9));
}

//==============================================================================
namespace
{
int firstSampleAbove (const juce::AudioBuffer<float>& b, int from, float threshold = 1e-3f)
{
    for (int i = std::max (0, from); i < b.getNumSamples(); ++i)
        if (std::abs (b.getSample (0, i)) > threshold || std::abs (b.getSample (1, i)) > threshold)
            return i;
    return -1;
}
} // namespace

TEST_CASE ("Synthesised kit: 3 velocity layers x 3 round robins per element, all audible", "[drums][sampler]")
{
    const auto kit = synth::DrumKit::synthesised (44100.0);
    for (int e = 0; e < numDrumElements; ++e)
    {
        INFO (elementName ((DrumElement) e));
        REQUIRE (kit.layers[(size_t) e].size() == 3);
        for (const auto& rrs : kit.layers[(size_t) e])
        {
            REQUIRE (rrs.size() == 3);
            for (const auto& b : rrs)
                CHECK (b.getMagnitude (0, 0, b.getNumSamples()) > 0.3f);
            CHECK (rrs[0].getSample (0, 500) != rrs[1].getSample (0, 500)); // round robins differ
        }
        CHECK (kit.layers[(size_t) e][0][0].getMagnitude (0, 0, kit.layers[(size_t) e][0][0].getNumSamples())
               < kit.layers[(size_t) e][2][0].getMagnitude (0, 0, kit.layers[(size_t) e][2][0].getNumSamples()));
    }
}

TEST_CASE ("Sampler render: kicks start on their sample, round robin, solo, choke", "[drums][sampler]")
{
    const double sr = 44100.0;
    const auto kit = synth::DrumKit::synthesised (sr);
    const std::vector<double> downbeats { 0.25, 1.1371, 2.0, 2.77 };
    std::vector<DrumNote> notes;
    for (double t : downbeats)
        notes.push_back ({ t, t + 0.1, DrumElement::kick, 100, 0, 0 });

    // each kick alone (a kick's 0.5 s tail would hide the next onset): placed on its sample
    for (double t : downbeats)
    {
        const std::vector<DrumNote> one { { t, t + 0.1, DrumElement::kick, 100, 0, 0 } };
        const auto audio = synth::renderDrums (one, kit, sr, (int) (3.5 * sr));
        const int expected = (int) std::lround (t * sr);
        INFO ("kick at " << t);
        CHECK (std::abs (firstSampleAbove (audio, 0) - expected) <= (int) (0.001 * sr)); // within 1 ms (skill: 5 ms)
    }
    // successive hits of one element use different round robins
    const auto audio = synth::renderDrums (notes, kit, sr, (int) (3.5 * sr));
    const int a0 = (int) std::lround (downbeats[0] * sr), a1 = (int) std::lround (downbeats[2] * sr);
    CHECK (audio.getSample (0, a0 + 300) != audio.getSample (0, a1 + 300));

    // per-element render: only the snare
    notes.push_back ({ 0.5, 0.6, DrumElement::snare, 100, 0, 4 });
    const auto snareOnly = synth::renderDrums (notes, kit, sr, (int) (1.0 * sr), DrumElement::snare);
    CHECK (firstSampleAbove (snareOnly, 0) >= (int) (0.5 * sr) - 1);
    CHECK (synth::drumsToMidi (notes, DrumElement::snare).getNumEvents() == 2);
    CHECK (synth::drumsToMidi (notes).getNumEvents() == 10);
    CHECK (synth::drumsToMidi (notes).getEventPointer (0)->message.getChannel() == 10);

    // a closed hat chokes a ringing open hat
    const std::vector<DrumNote> open { { 0.0, 0.5, DrumElement::openhat, 110, 0, 0 } };
    const std::vector<DrumNote> choked { { 0.0, 0.5, DrumElement::openhat, 110, 0, 0 }, { 0.1, 0.12, DrumElement::hat, 1, 0, 1 } };
    const auto a = synth::renderDrums (open, kit, sr, (int) (0.4 * sr)), b = synth::renderDrums (choked, kit, sr, (int) (0.4 * sr));
    const int at = (int) (0.25 * sr); // after the closed hat (velocity 1: nearly silent) has died out
    CHECK (std::abs (a.getSample (1, at)) > 1e-4f);
    CHECK (b.getMagnitude (1, at, 2000) < 1e-4f);
}

TEST_CASE ("User kit folder: named files replace synthesised elements", "[drums][sampler]")
{
    test::TempDir tmp;
    auto write = [&] (const juce::String& name, float freq) {
        juce::AudioBuffer<float> b (1, 4800);
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, 0.5f * std::sin (6.2831853f * freq * (float) i / 48000.0f));
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (tmp.dir.getChildFile (name));
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
        REQUIRE (w != nullptr);
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    };
    write ("kick.wav", 60.0f);
    write ("snare_v1_rr1.wav", 200.0f);
    write ("snare_v1_rr2.wav", 210.0f);
    write ("snare_v2_rr1.wav", 220.0f);
    write ("notes.wav", 100.0f); // not an element: ignored

    juce::String err;
    const auto kit = synth::DrumKit::loadFolder (tmp.dir, 44100.0, err);
    REQUIRE (kit.has_value());
    CHECK (kit->layers[(size_t) DrumElement::kick].size() == 1);
    CHECK (kit->layers[(size_t) DrumElement::kick][0].size() == 1);
    CHECK (kit->layers[(size_t) DrumElement::kick][0][0].getNumSamples() == 4410); // resampled 48 -> 44.1 kHz
    CHECK (kit->layers[(size_t) DrumElement::snare].size() == 2);
    CHECK (kit->layers[(size_t) DrumElement::snare][0].size() == 2);
    CHECK (kit->layers[(size_t) DrumElement::hat].size() == 3); // synthesised

    test::TempDir empty;
    CHECK_FALSE (synth::DrumKit::loadFolder (empty.dir, 44100.0, err).has_value());
    CHECK (err.contains ("no <element>"));
}

TEST_CASE ("Drum sampler renders without allocating in realtime sections", "[drums][sampler][rt]")
{
    if (! rt::isTrapInstalled())
        SKIP ("realtime trap not linked; run tools/rt_check.sh");
    const auto kit = synth::DrumKit::synthesised (44100.0);
    synth::DrumSampler sampler (kit);
    juce::AudioBuffer<float> out (2, 256);
    juce::MidiBuffer midi;
    midi.ensureSize (1024);
    rt::resetStats();
    for (int block = 0; block < 64; ++block)
    {
        midi.clear();
        if (block % 4 == 0)
            midi.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 110), 17); // fits the reserved space
        if (block % 2 == 1)
            midi.addEvent (juce::MidiMessage::noteOn (10, 42, (juce::uint8) 90), 3);
        out.clear();
        const rt::ScopedRealtimeSection rtSection;
        sampler.renderNextBlock (out, midi, 0, out.getNumSamples());
    }
    CHECK (rt::getStats().allocations == 0);
    CHECK (rt::getStats().deallocations == 0);
}
