// fx-chain: per-channel insert FX. Each stage does what it says, everything off is bit-transparent,
// and the acceptance test: no zipper noise when any parameter is swept (spectral test), with a
// negative control showing the test catches an unsmoothed parameter.
#include "TestSignals.h"

#include "app/core/RealtimeTrap.h"
#include "engine/fx/FxChain.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <juce_dsp/juce_dsp.h>

#include <functional>

using namespace jaudios;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
constexpr double sr = 44100.0;
constexpr int block = 128;

struct Rig
{
    fx::ParameterHost host;
    fx::FxChain chain;
    Rig()
    {
        chain.bind (host.state());
    }
    void set (const char* id, float plain) { REQUIRE (fx::setParameter (host.state(), id, plain)); }
    void setNormalised (const char* id, float v) { host.state().getParameter (id)->setValueNotifyingHost (v); }

    /** Renders `in` through the chain in `block`-sample blocks; `perBlock(pos)` runs before each. */
    juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& in, const std::function<void (int)>& perBlock = {},
                                     const float* kicks = nullptr, const juce::AudioBuffer<float>* stem = nullptr)
    {
        chain.prepare (sr, block);
        juce::AudioBuffer<float> out (in);
        for (int pos = 0; pos < out.getNumSamples(); pos += block)
        {
            if (perBlock)
                perBlock (pos);
            const int n = std::min (block, out.getNumSamples() - pos);
            float* chans[2] = { out.getWritePointer (0, pos), out.getWritePointer (1, pos) };
            fx::SidechainKey key { kicks != nullptr ? kicks + pos : nullptr,
                                   stem != nullptr ? stem->getReadPointer (0, pos) : nullptr,
                                   stem != nullptr ? stem->getReadPointer (1, pos) : nullptr };
            chain.process (chans, 2, n, key);
        }
        return out;
    }
};

double rms (const juce::AudioBuffer<float>& b, int ch, int from, int to)
{
    double s = 0;
    for (int i = from; i < to; ++i)
        s += (double) b.getSample (ch, i) * b.getSample (ch, i);
    return std::sqrt (s / std::max (1, to - from));
}

/** Level (RMS) of the `freq` component in [from, to) of channel 0, by correlation. */
double toneLevel (const juce::AudioBuffer<float>& b, double freq, int from, int to)
{
    double re = 0, im = 0;
    for (int i = from; i < to; ++i)
    {
        const double ph = juce::MathConstants<double>::twoPi * freq * i / sr;
        re += b.getSample (0, i) * std::cos (ph);
        im += b.getSample (0, i) * std::sin (ph);
    }
    return std::sqrt (re * re + im * im) / (to - from) * std::sqrt (2.0);
}

//==============================================================================
// The zipper metric: the loudest 1024-sample Hann frame's energy between 6 and 16 kHz (both
// channels), relative to the input's frame energy. A 440 Hz tone has nothing up there, so what
// a parameter change puts there is the zipper / click energy.
double maxHighBandEnergy (const juce::AudioBuffer<float>& b)
{
    constexpr int order = 10, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> window (size), data (2 * size);
    juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), size, juce::dsp::WindowingFunction<float>::hann, false);
    const int lo = (int) (6000.0 * size / sr), hi = (int) (16000.0 * size / sr);
    double worst = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int start = 0; start + size <= b.getNumSamples(); start += size / 4)
        {
            std::fill (data.begin(), data.end(), 0.0f);
            for (int i = 0; i < size; ++i)
                data[(size_t) i] = b.getSample (ch, start + i) * window[(size_t) i];
            fft.performFrequencyOnlyForwardTransform (data.data());
            double e = 0;
            for (int k = lo; k <= hi; ++k)
                e += (double) data[(size_t) k] * data[(size_t) k];
            worst = std::max (worst, e);
        }
    return worst;
}

double frameEnergyOf (const juce::AudioBuffer<float>& in)
{
    // the same frame, all bins: the reference for "relative to the signal"
    constexpr int size = 1024;
    std::vector<float> window (size);
    juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), size, juce::dsp::WindowingFunction<float>::hann, false);
    double e = 0;
    for (int i = 0; i < size; ++i)
        e += std::pow (in.getSample (0, i) * window[(size_t) i], 2.0);
    return e * size; // Parseval for the unnormalised FFT
}

std::shared_ptr<juce::AudioBuffer<float>> sweepSignal() { return test::makeSine (2, (int) (3.0 * sr), 440.0, sr, 0.25f); }

std::vector<float> kickTrain (int numSamples, double everySeconds = 0.5)
{
    std::vector<float> k ((size_t) numSamples, 0.0f);
    for (double t = 0.05; t * sr < numSamples; t += everySeconds)
        k[(size_t) (t * sr)] = 1.0f;
    return k;
}

/** The stage that owns `p` switched on, with settings that make every parameter audible. */
void enableStageFor (Rig& rig, fx::Param p)
{
    const std::string id = fx::spec (p).id;
    if (id.rfind ("filter", 0) == 0) { rig.set ("filter_on", 1); rig.set ("filter_cutoff", 2000); rig.set ("filter_resonance", 2); }
    if (id.rfind ("sat", 0) == 0)    { rig.set ("sat_on", 1); rig.set ("sat_drive", 12); }
    if (id.rfind ("ott", 0) == 0)    { rig.set ("ott_on", 1); rig.set ("ott_depth", 1); }
    if (id.rfind ("duck", 0) == 0)   { rig.set ("duck_on", 1); rig.set ("duck_depth", 0.8f); }
    if (id.rfind ("delay", 0) == 0)  { rig.set ("delay_on", 1); rig.set ("delay_mix", 0.5f); }
    if (id.rfind ("reverb", 0) == 0) { rig.set ("reverb_on", 1); rig.set ("reverb_mix", 0.5f); }
}

struct ZipperResult { double swept, staticWorst, floor; };

/** Sweeps parameter `p` the worst way a UI or automation can: alternating jumps between its
    extremes every 4096 samples, then a staircase of a new value every block, against the worst of
    static renders at five fixed values (same stage settings). */
ZipperResult zipperFor (fx::Param p)
{
    const auto in = sweepSignal();
    const auto kicks = kickTrain (in->getNumSamples());
    juce::AudioBuffer<float> stem (2, in->getNumSamples());
    stem.clear();
    for (int i = 0; i + 400 < stem.getNumSamples(); i += (int) (0.5 * sr))
        for (int j = 0; j < 400; ++j)
            for (int ch = 0; ch < 2; ++ch)
                stem.setSample (ch, i + j, 0.5f * std::exp ((float) -j / 80.0f) * (float) std::sin (j * 0.05)); // a soft kick
    const char* id = fx::spec (p).id;
    const bool usesStem = std::string (id) == "duck_source";

    double staticWorst = 0;
    for (float v : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        Rig rig;
        enableStageFor (rig, p);
        rig.setNormalised (id, v);
        staticWorst = std::max (staticWorst, maxHighBandEnergy (rig.render (*in, {}, kicks.data(), usesStem ? &stem : nullptr)));
    }
    Rig rig;
    enableStageFor (rig, p);
    const int half = in->getNumSamples() / 2;
    const auto swept = rig.render (*in, [&] (int pos) {
        if (pos < half)
            rig.setNormalised (id, (pos / 4096) % 2 == 0 ? 0.0f : 1.0f);
        else
            rig.setNormalised (id, (float) ((pos - half) % (int) sr) / (float) sr); // a new value every block
    }, kicks.data(), usesStem ? &stem : nullptr);
    if (const auto dump = juce::SystemStats::getEnvironmentVariable ("JAUDIOS_FX_DUMP", {}); dump.isNotEmpty())
    {
        juce::FileOutputStream os (juce::File (dump).getChildFile (juce::String (id) + ".f32"));
        os.setPosition (0);
        os.truncate();
        for (int i = 0; i < swept.getNumSamples(); ++i)
            os.writeFloat (swept.getSample (0, i));
    }
    return { maxHighBandEnergy (swept), staticWorst, 1e-6 * frameEnergyOf (*in) }; // floor: -60 dB re the tone
}
} // namespace

//==============================================================================
TEST_CASE ("The FX parameter layout matches the specs and takes plain values", "[fx]")
{
    fx::ParameterHost host;
    auto& state = host.state();
    for (const auto& s : fx::paramSpecs())
    {
        INFO (s.id);
        auto* p = state.getParameter (s.id);
        REQUIRE (p != nullptr);
        CHECK_THAT (p->convertFrom0to1 (p->getValue()), WithinAbs (s.defaultValue, 1e-3));
    }
    CHECK (fx::setParameter (state, "filter_cutoff", 1000.0f));
    CHECK_THAT (state.getRawParameterValue ("filter_cutoff")->load(), WithinRel (1000.0f, 1e-3f));
    CHECK (fx::setParameter (state, "delay_division", juce::String ("1/4 dotted")));
    CHECK (state.getRawParameterValue ("delay_division")->load() == 4.0f);
    CHECK (fx::setParameter (state, "reverb_on", juce::String ("on")));
    CHECK (fx::setParameter (state, "filter_cutoff", 99999.0f)); // clamped to the range
    CHECK_THAT (state.getRawParameterValue ("filter_cutoff")->load(), WithinRel (20000.0f, 1e-3f));
    CHECK_FALSE (fx::setParameter (state, "no_such_param", 1.0f));
    CHECK_FALSE (fx::setParameter (state, "delay_division", juce::String ("1/3")));
}

TEST_CASE ("With every stage off the chain is bit-transparent", "[fx]")
{
    Rig rig;
    const auto in = test::makeSine (2, 20000, 330.0, sr);
    const auto out = rig.render (*in);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < in->getNumSamples(); ++i)
            REQUIRE (out.getSample (ch, i) == in->getSample (ch, i));
    CHECK_FALSE (rig.chain.isActive());
}

TEST_CASE ("Filter: low-, high- and band-pass around the cutoff", "[fx]")
{
    auto level = [] (const char* type, double freq) {
        Rig rig;
        rig.set ("filter_on", 1);
        REQUIRE (fx::setParameter (rig.host.state(), "filter_type", juce::String (type)));
        rig.set ("filter_cutoff", 1000.0f);
        const auto out = rig.render (*test::makeSine (2, 22050, freq, sr, 0.5f));
        return juce::Decibels::gainToDecibels (toneLevel (out, freq, 11025, 22050) / (0.5 / std::sqrt (2.0)));
    };
    CHECK (level ("Low-pass", 100.0) > -0.5);
    CHECK (level ("Low-pass", 8000.0) < -30.0);  // 12 dB/oct, three octaves up
    CHECK (level ("High-pass", 8000.0) > -0.5);
    CHECK (level ("High-pass", 100.0) < -35.0);
    CHECK (std::abs (level ("Band-pass", 1000.0)) < 0.5); // unity at the centre
    CHECK (level ("Band-pass", 8000.0) < -15.0);
}

TEST_CASE ("Saturation rounds off peaks and adds odd harmonics", "[fx]")
{
    Rig rig;
    rig.set ("sat_on", 1);
    rig.set ("sat_drive", 18.0f);
    const auto in = test::makeSine (2, 22050, 200.0, sr, 0.8f);
    const auto out = rig.render (*in);
    CHECK (out.getMagnitude (0, 11025, 11025) < in->getMagnitude (0, 0, 22050) * 0.5f);
    CHECK (toneLevel (out, 600.0, 11025, 22050) > 0.1 * toneLevel (out, 200.0, 11025, 22050)); // 3rd harmonic
    // quiet signals pass at unity (small-signal gain 1)
    Rig quiet;
    quiet.set ("sat_on", 1);
    quiet.set ("sat_drive", 18.0f);
    const auto soft = quiet.render (*test::makeSine (2, 22050, 200.0, sr, 0.001f));
    CHECK_THAT (rms (soft, 0, 11025, 22050), WithinRel (0.001 / std::sqrt (2.0), 0.01));
}

TEST_CASE ("OTT: quiet bands come up, loud bands go down, depth 0 leaves the level alone", "[fx]")
{
    auto gainDb = [] (float depth, float amplitude) {
        Rig rig;
        rig.set ("ott_on", 1);
        rig.set ("ott_depth", depth);
        const auto in = test::makeSine (2, (int) sr, 1000.0, sr, amplitude);
        const auto out = rig.render (*in);
        return juce::Decibels::gainToDecibels (rms (out, 0, 33075, 44100) / rms (*in, 0, 33075, 44100));
    };
    CHECK (gainDb (1.0f, 0.003f) > 10.0); // -50 dBFS comes up
    CHECK (gainDb (1.0f, 0.9f) < -10.0);  // -1 dBFS goes down
    CHECK (std::abs (gainDb (0.0f, 0.003f)) < 0.2);
    CHECK (std::abs (gainDb (0.0f, 0.9f)) < 0.2);
    // silence stays silence (no upward gain below the floor)
    Rig rig;
    rig.set ("ott_on", 1);
    rig.set ("ott_depth", 1.0f);
    const auto out = rig.render (*test::makeSine (2, (int) sr, 1000.0, sr, 1e-5f));
    CHECK (out.getMagnitude (0, 33075, 11025) < 1e-4f);
}

TEST_CASE ("Ducker: kick events and the drums stem pump the channel", "[fx]")
{
    const auto in = test::makeSine (2, (int) sr, 220.0, sr, 0.5f);
    const auto kicks = kickTrain (in->getNumSamples());
    Rig rig;
    rig.set ("duck_on", 1);
    rig.set ("duck_depth", 0.8f);
    rig.set ("duck_release", 150.0f);
    const auto out = rig.render (*in, {}, kicks.data());
    const int kick = (int) (0.55 * sr); // the second kick
    const double before = rms (out, 0, kick - 1000, kick);
    CHECK_THAT (before, WithinRel (0.5 / std::sqrt (2.0), 0.05)); // the first kick's release has nearly ended
    CHECK (rms (out, 0, kick + 200, kick + 600) < 0.3 * before);        // ducked by ~depth
    CHECK (rms (out, 0, kick + 15000, kick + 16000) > 0.9 * before);  // back after the release

    juce::AudioBuffer<float> stem (2, in->getNumSamples());
    stem.clear();
    for (int j = 0; j < 2000; ++j)
        for (int ch = 0; ch < 2; ++ch)
            stem.setSample (ch, kick + j, 0.5f * std::exp ((float) -j / 400.0f) * (float) std::sin (j * 0.03));
    Rig fromStem;
    fromStem.set ("duck_on", 1);
    fromStem.set ("duck_depth", 0.8f);
    REQUIRE (fx::setParameter (fromStem.host.state(), "duck_source", juce::String ("Drums stem")));
    const auto o2 = fromStem.render (*in, {}, kicks.data(), &stem); // kicks ignored in stem mode
    CHECK (rms (o2, 0, (int) (0.05 * sr) + 200, (int) (0.05 * sr) + 600) > 0.9 * before);
    CHECK (rms (o2, 0, kick + 300, kick + 700) < 0.5 * before);
}

TEST_CASE ("Ping-pong delay echoes on the synced time, alternating sides, decaying by the feedback", "[fx]")
{
    Rig rig;
    rig.chain.setTempo (120.0);
    rig.set ("delay_on", 1);
    rig.set ("delay_mix", 1.0f);
    rig.set ("delay_feedback", 0.5f);
    REQUIRE (fx::setParameter (rig.host.state(), "delay_division", juce::String ("1/8 dotted"))); // 0.375 s at 120
    juce::AudioBuffer<float> in (2, (int) (1.5 * sr));
    in.clear();
    in.setSample (0, 1000, 1.0f); // after the 10 ms switch-on fade of the send
    in.setSample (1, 1000, 1.0f);
    const auto out = rig.render (in);
    const int d = (int) std::lround (0.375 * sr);
    CHECK_THAT (out.getSample (0, 1000 + d), WithinAbs (1.0, 1e-4));     // first echo, left (mono sum)
    CHECK_THAT (out.getSample (1, 1000 + d), WithinAbs (0.0, 1e-6));
    CHECK_THAT (out.getSample (1, 1000 + 2 * d), WithinAbs (0.5, 1e-4)); // second, right, x feedback
    CHECK_THAT (out.getSample (0, 1000 + 3 * d), WithinAbs (0.25, 1e-4)); // third, left
    // tempo follows: 1/8 dotted at 90 bpm = 0.5 s
    Rig slow;
    slow.chain.setTempo (90.0);
    slow.set ("delay_on", 1);
    slow.set ("delay_mix", 1.0f);
    const auto o2 = slow.render (in);
    CHECK_THAT (o2.getSample (0, 1000 + (int) std::lround (0.5 * sr)), WithinAbs (1.0, 1e-4));
}

TEST_CASE ("Reverb adds a decaying tail; mix 0 is dry", "[fx]")
{
    juce::AudioBuffer<float> in (2, (int) (2.0 * sr));
    in.clear();
    for (int i = 0; i < 200; ++i)
        for (int ch = 0; ch < 2; ++ch)
            in.setSample (ch, 1000 + i, (float) std::sin (i * 0.3));
    Rig rig;
    rig.set ("reverb_on", 1);
    rig.set ("reverb_mix", 0.5f);
    const auto out = rig.render (in);
    const double early = rms (out, 0, (int) (0.1 * sr), (int) (0.3 * sr));
    const double late = rms (out, 0, (int) (1.0 * sr), (int) (1.2 * sr));
    CHECK (early > 1e-3);
    CHECK (late < early);
    Rig dry;
    dry.set ("reverb_on", 1);
    dry.set ("reverb_mix", 0.0f);
    const auto o2 = dry.render (in);
    CHECK (o2.getMagnitude (0, (int) (0.1 * sr), (int) (0.5 * sr)) == 0.0f);
}

TEST_CASE ("The zipper test catches an unsmoothed parameter (negative control)", "[fx][zipper]")
{
    // the same protocol applied to a naive gain that jumps between 0 and 1 on block boundaries
    const auto in = sweepSignal();
    juce::AudioBuffer<float> stepped (*in);
    for (int pos = 0; pos < stepped.getNumSamples(); pos += block)
        stepped.applyGain (pos, std::min (block, stepped.getNumSamples() - pos), (pos / 4096) % 2 == 0 ? 0.0f : 1.0f);
    const double floor = 1e-6 * frameEnergyOf (*in);
    const double zipper = maxHighBandEnergy (stepped);
    WARN ("naive step: " << juce::Decibels::gainToDecibels (std::sqrt (zipper / frameEnergyOf (*in))) << " dB re the tone");
    CHECK (zipper > 100.0 * (maxHighBandEnergy (*in) + floor)); // > 20 dB over the pass threshold
}

TEST_CASE ("No zipper noise sweeping any parameter (spectral test)", "[fx][zipper]")
{
    const auto p = (fx::Param) GENERATE (range (0, fx::numParams));
    const auto r = zipperFor (p);
    INFO (fx::spec (p).id << ": swept " << r.swept << ", static worst " << r.staticWorst << ", floor " << r.floor);
    WARN (fx::spec (p).id << ": swept " << juce::Decibels::gainToDecibels (std::sqrt (r.swept / (r.floor * 1e6)))
                          << " dB, static worst " << juce::Decibels::gainToDecibels (std::sqrt (r.staticWorst / (r.floor * 1e6))) << " dB (re the tone)");
    // a sweep may put no more than 6 dB more energy above 6 kHz than the worst fixed setting does,
    // or stay under -60 dB re the tone
    CHECK (r.swept <= 4.0 * r.staticWorst + r.floor);
}

TEST_CASE ("The FX chain processes without allocating", "[fx][rt]")
{
    if (! rt::isTrapInstalled())
        SKIP ("realtime trap not linked; run tools/rt_check.sh");
    Rig rig;
    for (const auto& s : fx::paramSpecs())
        if (s.kind == fx::ParamSpec::Kind::toggle)
            rig.set (s.id, 1.0f);
    rig.chain.prepare (sr, block);
    juce::AudioBuffer<float> buf (2, block);
    const auto kicks = kickTrain (block * 400);
    rt::resetStats();
    for (int b = 0; b < 400; ++b)
    {
        for (int i = 0; i < block; ++i)
            for (int ch = 0; ch < 2; ++ch)
                buf.setSample (ch, i, 0.3f * (float) std::sin ((b * block + i) * 0.05));
        // parameters change while it runs (as from the UI): every continuous one, every 10 blocks
        if (b % 10 == 0)
            for (const auto& s : fx::paramSpecs())
                if (s.kind == fx::ParamSpec::Kind::range)
                    rig.setNormalised (s.id, (float) ((b / 10) % 5) / 4.0f);
        float* chans[2] = { buf.getWritePointer (0), buf.getWritePointer (1) };
        const rt::ScopedRealtimeSection section;
        rig.chain.process (chans, 2, block, { kicks.data() + b * block, buf.getReadPointer (0), buf.getReadPointer (1) });
    }
    CHECK (rt::getStats().allocations == 0);
    CHECK (rt::getStats().deallocations == 0);
}

