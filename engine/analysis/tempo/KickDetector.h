#pragma once

#include <vector>

namespace jaudios::analysis
{

struct KickSettings
{
    double lowpassHz = 110.0;      // kick fundamental band
    double minIntervalSec = 0.1;   // no two kicks closer than this
    double thresholdDb = 18.0;     // onset strength above the median, in dB of low-band energy rise
    double floorDb = -45.0;        // ignore onsets quieter than this below the loudest frame
    double absoluteFloorDb = -50.0; // and quieter than this low-band RMS (dBFS): no kick, no onset
};

/** Kick onsets (seconds) for the bass generator's sidechain hints: low-band (2nd-order low-pass)
    energy in 5.8 ms frames; an onset is a local maximum of the frame-to-frame energy rise (dB)
    that clears an adaptive threshold, a floor relative to the loudest frame and an absolute
    low-band level (so hats alone, whose low-band residue is tiny, never count). Best on the drums stem; on a full
    mix, bass-note onsets in the same band can also fire. Deterministic; analysis thread. */
std::vector<double> detectKicks (const float* mono, int numSamples, double sampleRate, const KickSettings& = {});

} // namespace jaudios::analysis
