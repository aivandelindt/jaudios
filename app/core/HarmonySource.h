#pragma once

#include "engine/analysis/Analysis.h"
#include "engine/analysis/transcription/AudioToMidi.h"
#include "engine/generators/ChordEngine.h"

namespace jaudios
{

/** Glue between analysis and the chord engine (which knows no analysis types): key + softmax
    margin, one bar per pair of downbeats with its chroma, melody and bass notes weighted by their
    transcription confidence. Without downbeats, bars of four beats (or 2 s) are used. */
generators::HarmonicSource makeHarmonicSource (const analysis::AnalysisResult&, double durationSec,
                                               const std::vector<analysis::transcription::Note>& melody,
                                               const std::vector<analysis::transcription::Note>& bass);

/** Splits one polyphonic transcription (e.g. of the mix) into melody and bass evidence: per
    moment the lowest note below `bassBelowMidi` is bass, everything else melody. */
void splitBass (const std::vector<analysis::transcription::Note>& notes, int bassBelowMidi,
                std::vector<analysis::transcription::Note>& melody, std::vector<analysis::transcription::Note>& bass);

} // namespace jaudios
