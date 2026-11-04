#pragma once

#include "KeyDetector.h"

#include "engine/analysis/Chroma.h"

namespace jaudios::analysis
{

/** Chroma + key-profile correlation (Krumhansl-Schmuckler method). No model, cheap.

    score_k = softmax_k (r_k / T), r_k = Pearson correlation of the track's chroma with the
    profile rotated to key k. Profile = mean of Krumhansl-Kessler and Temperley (both
    z-normalised). T is a calibration constant: it sets how peaked the softmax (and therefore
    the margin) is; it does not change the ranking. Re-fit T on GiantSteps (nightly) so margins
    are comparable across backends.

    Relative major/minor: profiles cannot separate e.g. Am-F-C-G from C major's vi-IV-I-V (same
    pitch classes; profiles favour the major by 0.2-0.3 r). Listeners use the chord the music
    opens on. So the winning pitch collection (Camelot number) keeps its profile score, and the
    mode within it is decided by
        (r_major - r_minor) + openingTonicWeight * (tonic(major) - tonic(minor))
    where tonic(k) = cosine of the opening chroma (first openingSec of tonal content) with k's
    tonic triad. Risk: major-key songs that open on vi. Calibrate the weight on GiantSteps.
*/
class KeyDetectorChroma final : public IKeyDetector
{
public:
    static constexpr double softmaxTemperature = 0.05;
    static constexpr double openingTonicWeight = 1.0; // ~ the typical profile gap within a relative pair (0.2-0.3 r)
    static constexpr double openingSec = 4.0;

    KeyResult analyzeFile (const juce::File&) override;
    KeyResult analyzeBuffer (const juce::AudioBuffer<float>&, double sampleRate) override;

    /** Key from chroma frames already computed by the rhythm stage (same audio, same rate). */
    static KeyResult analyzeFrames (const ChromaFrames&);

    /** Scores a precomputed global chroma (+ optional opening chroma; all-zero = none). */
    static KeyResult scoreChroma (const Chroma12& global, const Chroma12& opening = {});

    /** Sum of the first openingSec of frames with tonal content (skips silent / drum-only intros). */
    static Chroma12 openingChroma (const ChromaFrames&);
};

} // namespace jaudios::analysis
