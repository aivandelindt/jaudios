#pragma once

#include <vector>

namespace jaudios::analysis
{

/** Magnitude spectrogram, frame-major: mags[frame * numBins + bin]. Frames are centred:
    frame i covers [i*hop - fftSize/2, i*hop + fftSize/2) with zero padding outside the signal. */
struct Spectrogram
{
    int fftSize = 0, hop = 0, numFrames = 0, numBins = 0;
    double sampleRate = 0.0;
    std::vector<float> mags;

    const float* frame (int i) const noexcept { return mags.data() + (size_t) i * (size_t) numBins; }
    float* frame (int i) noexcept { return mags.data() + (size_t) i * (size_t) numBins; }
    double binHz (double bin) const noexcept { return bin * sampleRate / fftSize; }
    double frameTimeSec (int i) const noexcept { return (double) i * hop / sampleRate; }
};

/** Hann-windowed STFT magnitude. fftSize must be a power of two. Bins: 0 .. fftSize/2. */
Spectrogram computeSpectrogram (const float* mono, int numSamples, double sampleRate, int fftSize, int hop);

} // namespace jaudios::analysis
