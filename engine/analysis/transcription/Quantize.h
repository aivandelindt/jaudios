#pragma once

#include "AudioToMidi.h"

namespace jaudios::analysis::transcription
{

/** Beat-grid quantisation (audio-to-midi skill step 3: optional, default 1/16 with swing preservation). */
struct QuantizeSettings
{
    int division = 16;          // grid steps per whole note: 4 (quarter), 8, 16, 32; beats are quarter notes
    double strength = 1.0;      // 0 = unchanged .. 1 = on the grid
    bool preserveSwing = true;  // measure the performance's swing and quantise to a swung grid
    bool quantizeEnds = true;   // also snap note ends (kept >= one step after the start)
};

struct QuantizeResult
{
    std::vector<Note> notes;
    double swing = 0.0; // detected: offbeat steps sit at (k + swing) steps; 0 = straight, 1/3 = triplet feel
};

/** Snaps note starts (and ends) to a grid that subdivides every beat interval of `beats`
    (from tempo-and-beatgrid; extrapolated before the first and after the last beat), so tempo
    drift in the beat grid is followed. With preserveSwing, the median delay of notes nearest to
    odd grid steps, minus that of notes nearest to even steps (so latency is not swing; >= 4 notes,
    > 5 % of a step), becomes the swing of the target grid. */
QuantizeResult quantize (std::vector<Note> notes, const std::vector<double>& beats, const QuantizeSettings& = {});

} // namespace jaudios::analysis::transcription
