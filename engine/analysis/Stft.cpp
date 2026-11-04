#include "Stft.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace jaudios::analysis
{

Spectrogram computeSpectrogram (const float* mono, int numSamples, double sampleRate, int fftSize, int hop)
{
    jassert (juce::isPowerOfTwo (fftSize) && hop > 0);

    Spectrogram s;
    s.fftSize = fftSize;
    s.hop = hop;
    s.sampleRate = sampleRate;
    s.numBins = fftSize / 2 + 1;
    s.numFrames = numSamples > 0 ? numSamples / hop + 1 : 0;
    s.mags.assign ((size_t) s.numFrames * (size_t) s.numBins, 0.0f);

    juce::dsp::FFT fft (juce::roundToInt (std::log2 (fftSize)));
    std::vector<float> window ((size_t) fftSize), buffer ((size_t) fftSize * 2);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / fftSize));

    for (int f = 0; f < s.numFrames; ++f)
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        const int start = f * hop - fftSize / 2;
        for (int i = 0; i < fftSize; ++i)
        {
            const int n = start + i;
            if (n >= 0 && n < numSamples)
                buffer[(size_t) i] = mono[n] * window[(size_t) i];
        }

        fft.performFrequencyOnlyForwardTransform (buffer.data(), true);
        std::copy (buffer.begin(), buffer.begin() + s.numBins, s.frame (f));
    }

    return s;
}

} // namespace jaudios::analysis
