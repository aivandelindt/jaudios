#pragma once

#include <array>
#include <vector>

namespace jaudios::analysis
{

using Chroma12 = std::array<float, 12>; // index 0 = C

/** Frame-wise pitch-class profile (HPCP-style) of the harmonic part of the signal.

    1. STFT (16384 / hop 4096 @ 44.1 kHz: 2.7 Hz bins resolve semitones down to ~50 Hz).
    2. Median-filter HPSS (Fitzgerald 2010): soft mask keeps sustained partials, suppresses drums.
    3. Spectral peaks 50 Hz - 5 kHz, parabolic interpolation, global tuning estimate (cents).
    4. Each peak adds its magnitude to the nearest pitch class with a cos^2 weight over +-1 semitone,
       and (HPCP harmonic weighting) 0.6^(h-1) of it to the pitch class of f/h for h = 2..8.
*/
struct ChromaFrames
{
    double hopSec = 0.0;
    double frameSec = 0.0;
    double tuningCents = 0.0;      // deviation of the material from A4 = 440 Hz
    std::vector<Chroma12> frames;  // un-normalised (energy-weighted)

    double frameCentreSec (size_t i) const noexcept { return (double) i * hopSec; }
};

ChromaFrames computeChroma (const float* mono, int numSamples, double sampleRate);

/** Sum of frames whose centre lies in [startSec, endSec), preferring frames fully inside. */
Chroma12 sumChroma (const ChromaFrames&, double startSec, double endSec);

/** Scales so the maximum is 1 (all-zero stays all-zero). */
Chroma12 normalisedMax (Chroma12);

} // namespace jaudios::analysis
