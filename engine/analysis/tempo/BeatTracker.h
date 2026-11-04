#pragma once

#include "engine/analysis/Chroma.h"

#include <vector>

namespace jaudios::analysis
{

struct BarInfo
{
    double startSec = 0.0, endSec = 0.0;
    Chroma12 chroma {}; // max-normalised pitch-class profile of the bar (harmonic part)
};

struct RhythmResult
{
    double bpm = 0.0;                  // 0 when no pulse was found
    double autocorrBpm = 0.0;          // comb-filter estimate before grid fitting
    double tempoConfidence = 0.0;      // comb peak / mean comb score (1 = flat)
    std::vector<double> tempoAliases;  // half / double tempo of `bpm` within 60..200

    std::vector<double> beats;         // seconds
    std::vector<double> downbeats;     // seconds (subset of beats), 4/4 assumed in M1
    int beatsPerBar = 4;
    int downbeatPhase = 0;             // index of the first downbeat in `beats`

    bool fixedTempo = false;           // beats fit a constant-tempo grid
    double gridResidualMs = 0.0;       // RMS residual of that fit

    std::vector<BarInfo> bars;         // between consecutive downbeats
    double tuningCents = 0.0;          // from the chroma stage
    ChromaFrames chroma;               // reused by the chroma key detector (computed once)
};

/** Tempo, beats, downbeats and per-bar chroma for mono audio (any rate; 44.1 kHz expected).
    Deterministic. Analysis thread only. */
RhythmResult analyzeRhythm (const float* mono, int numSamples, double sampleRate);

} // namespace jaudios::analysis
