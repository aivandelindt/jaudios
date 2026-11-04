#include "TestSignals.h"

#include "engine/analysis/AudioImport.h"
#include "engine/dsp/CQT.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace jaudios;
using namespace jaudios::dsp;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE ("OpenKeyScan CQT geometry matches librosa's parameters", "[cqt]")
{
    const ConstantQ cqt (ConstantQ::openKeyScan());
    const auto& cfg = cqt.config();
    CHECK (cfg.sampleRate == 44100.0);
    CHECK (cfg.hopLength == 8820);
    CHECK (cfg.numBins == 105);
    CHECK (cfg.binsPerOctave == 24);
    CHECK (cfg.fmin == 65.0);

    // librosa.cqt_frequencies(105, fmin=65, bins_per_octave=24)
    CHECK_THAT (cqt.binFrequency (0), WithinRel (65.0, 1e-12));
    CHECK_THAT (cqt.binFrequency (24), WithinRel (130.0, 1e-12));
    CHECK_THAT (cqt.binFrequency (104), WithinRel (65.0 * std::pow (2.0, 104.0 / 24.0), 1e-12));

    // librosa.filters.wavelet_lengths(freqs, sr=44100) with alpha from _relative_bandwidth:
    // interior bins Q = 1 / alpha, alpha = (2^(2/24) - 1) / (2^(2/24) + 1)
    const double r = std::pow (2.0, 2.0 / 24.0);
    const double alpha = (r - 1.0) / (r + 1.0);
    CHECK_THAT (cqt.filterLength (50), WithinRel (44100.0 / (alpha * cqt.binFrequency (50)), 1e-12));
}

TEST_CASE ("CQT frame count and model-input layout", "[cqt]")
{
    const ConstantQ cqt (ConstantQ::openKeyScan());
    const int n = 44100 * 20;
    const auto tone = test::makeSine (1, n, 440.0, 44100.0);

    const auto mag = cqt.magnitude (tone->getReadPointer (0), n);
    CHECK (mag.numBins == 105);
    CHECK (mag.numFrames == 1 + n / 8820); // librosa stft(center=True): 1 + len // hop

    const auto input = openKeyScanModelInput (cqt, tone->getReadPointer (0), n);
    CHECK (input.numBins == 105);
    CHECK (input.numFrames == mag.numFrames - 2); // spec[:, 0:-2]
    REQUIRE (input.values.size() == (size_t) 105 * (size_t) input.numFrames); // bin-major (105, T)
    CHECK_THAT (input.at (10, 3), WithinAbs (std::log1p (mag.at (10, 3)), 1e-6));
}

TEST_CASE ("CQT of a pure tone peaks at its bin", "[cqt]")
{
    const ConstantQ cqt (ConstantQ::openKeyScan());
    const int bin = GENERATE (6, 30, 57, 90); // one per octave stage (11.025 / 22.05 / 44.1 kHz)
    const double f = cqt.binFrequency (bin);
    const int n = 44100 * 10;
    const auto tone = test::makeSine (1, n, f, 44100.0);

    const auto mag = cqt.magnitude (tone->getReadPointer (0), n);
    const int frame = mag.numFrames / 2;
    int best = 0;
    for (int b = 1; b < mag.numBins; ++b)
        if (mag.at (b, frame) > mag.at (best, frame))
            best = b;
    CHECK (best == bin);
}
