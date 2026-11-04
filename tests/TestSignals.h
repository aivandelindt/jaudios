#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <memory>

namespace jaudios::test
{

inline std::shared_ptr<juce::AudioBuffer<float>> makeSine (int numChannels, int numSamples, double freq,
                                                           double sampleRate, float amplitude = 0.5f)
{
    auto b = std::make_shared<juce::AudioBuffer<float>> (numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            b->setSample (ch, i, amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sampleRate
                                                               + 0.3 * ch));
    return b;
}

/** Temp directory removed on destruction. */
struct TempDir
{
    TempDir() : dir (juce::File::createTempFile ("jaudios_test")) { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
    juce::File dir;
};

} // namespace jaudios::test
