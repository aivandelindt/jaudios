#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace jaudios::analysis
{

/** Offline, zero-phase band-limited resampler (Kaiser-windowed sinc, table-driven).

    - Arbitrary ratios; cutoff follows min(source, target) Nyquist, so downsampling is
      anti-aliased (JUCE's GenericInterpolator is not).
    - Zero phase: an event at time t in the input stays at time t in the output.
    - Identity (bit-exact copy) when the rates are equal.
    - Deterministic: same input -> byte-identical output.

    Output length = round (numInputSamples * targetRate / sourceRate).
    Not realtime-safe (allocates); analysis/import thread only.
*/
juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& input, double sourceRate, double targetRate);

} // namespace jaudios::analysis
