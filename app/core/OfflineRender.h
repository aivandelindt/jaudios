#pragma once

#include "AudioEngine.h"
#include "RealtimeTrap.h"

#include <vector>

namespace jaudios
{

struct OfflineRenderReport
{
    juce::AudioBuffer<float> output;  // stereo, same length as the source
    int blockSize = 0;
    int numBlocks = 0;
    double latencyMs = 0.0;
    float maxAbsDiffVsSource = 0.0f;  // null test (unity gain path)
    rt::Stats rtStats;                // counted only when the RT trap is installed
};

/** Plays `source` through the full AudioEngine graph in fixed-size blocks, exactly like the
    device callback would, and collects realtime-safety statistics.
    `deviceBlockSizes` (cycled) simulates a device delivering varying callback sizes; empty means
    every callback is `blockSize`. */
OfflineRenderReport renderThroughEngine (AudioEngine& engine, std::shared_ptr<const juce::AudioBuffer<float>> source,
                                         double sampleRate, int blockSize,
                                         std::vector<int> deviceBlockSizes = {});

} // namespace jaudios
