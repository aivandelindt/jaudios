#include "TestSignals.h"

#include "engine/analysis/Resampler.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace jaudios::analysis;
using Catch::Matchers::WithinAbs;

namespace
{
/** Amplitude of `freq` in x via single-bin DFT over the central part (skips edges). */
double toneAmplitude (const float* x, int n, double freq, double sr)
{
    const int start = n / 8, end = n - n / 8;
    double re = 0.0, im = 0.0, wsum = 0.0;
    for (int i = start; i < end; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (i - start) / (end - start));
        const double ph = juce::MathConstants<double>::twoPi * freq * i / sr;
        re += w * x[i] * std::cos (ph);
        im += w * x[i] * std::sin (ph);
        wsum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / wsum;
}

double rms (const float* x, int n)
{
    const int start = n / 8, end = n - n / 8;
    double s = 0.0;
    for (int i = start; i < end; ++i)
        s += (double) x[i] * x[i];
    return std::sqrt (s / (end - start));
}
} // namespace

TEST_CASE ("Resampler is the identity when rates match", "[capture][resampler]")
{
    auto in = jaudios::test::makeSine (2, 1000, 440.0, 44100.0);
    const auto out = resample (*in, 44100.0, 44100.0);

    REQUIRE (out.getNumSamples() == 1000);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 1000; ++i)
            REQUIRE (out.getSample (ch, i) == in->getSample (ch, i));
}

TEST_CASE ("Resampler output length follows the rate ratio", "[capture][resampler]")
{
    const double from = GENERATE (22050.0, 32000.0, 48000.0, 88200.0, 96000.0, 192000.0);
    juce::AudioBuffer<float> in (1, (int) from); // 1 second
    in.clear();

    const auto out = resample (in, from, 44100.0);
    CHECK (out.getNumSamples() == 44100);
}

TEST_CASE ("Resampler preserves tone frequency and amplitude", "[capture][resampler]")
{
    const double from = GENERATE (22050.0, 48000.0, 96000.0);
    const double freq = 1000.0;

    auto in = jaudios::test::makeSine (1, (int) from, freq, from, 0.5f);
    const auto out = resample (*in, from, 44100.0);

    const double amp = toneAmplitude (out.getReadPointer (0), out.getNumSamples(), freq, 44100.0);
    CHECK_THAT (juce::Decibels::gainToDecibels (amp / 0.5), WithinAbs (0.0, 0.05));
}

TEST_CASE ("Resampler keeps transients time-aligned (zero phase)", "[capture][resampler]")
{
    const double from = GENERATE (48000.0, 96000.0, 22050.0);
    const double impulseSec = 0.1;

    juce::AudioBuffer<float> in (1, (int) (from * 0.25));
    in.clear();
    in.setSample (0, (int) std::lround (impulseSec * from), 1.0f);

    const auto out = resample (in, from, 44100.0);

    int peakIndex = 0;
    for (int i = 1; i < out.getNumSamples(); ++i)
        if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, peakIndex)))
            peakIndex = i;

    CHECK (std::abs (peakIndex - impulseSec * 44100.0) <= 1.0);
}

TEST_CASE ("Resampler rejects content above the target Nyquist", "[capture][resampler]")
{
    SECTION ("30 kHz at 96 kHz")
    {
        auto in = jaudios::test::makeSine (1, 96000, 30000.0, 96000.0, 0.5f);
        const auto out = resample (*in, 96000.0, 44100.0);
        CHECK (juce::Decibels::gainToDecibels (rms (out.getReadPointer (0), out.getNumSamples()) / (0.5 / std::sqrt (2.0)))
               < -60.0);
    }
    SECTION ("23 kHz at 48 kHz")
    {
        auto in = jaudios::test::makeSine (1, 48000, 23000.0, 48000.0, 0.5f);
        const auto out = resample (*in, 48000.0, 44100.0);
        CHECK (juce::Decibels::gainToDecibels (rms (out.getReadPointer (0), out.getNumSamples()) / (0.5 / std::sqrt (2.0)))
               < -40.0);
    }
}

TEST_CASE ("Resampler passband is flat to 18 kHz", "[capture][resampler]")
{
    const double freq = GENERATE (100.0, 5000.0, 12000.0, 18000.0);
    auto in = jaudios::test::makeSine (1, 48000, freq, 48000.0, 0.5f);
    const auto out = resample (*in, 48000.0, 44100.0);

    const double amp = toneAmplitude (out.getReadPointer (0), out.getNumSamples(), freq, 44100.0);
    CHECK_THAT (juce::Decibels::gainToDecibels (amp / 0.5), WithinAbs (0.0, 0.1));
}

TEST_CASE ("Resampler is deterministic", "[capture][resampler]")
{
    auto in = jaudios::test::makeSine (2, 48000, 440.0, 48000.0);
    const auto a = resample (*in, 48000.0, 44100.0);
    const auto b = resample (*in, 48000.0, 44100.0);

    REQUIRE (a.getNumSamples() == b.getNumSamples());
    for (int ch = 0; ch < 2; ++ch)
        REQUIRE (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) == 0);
}
