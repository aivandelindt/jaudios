#include "OfflineRender.h"

#include <algorithm>
#include <cmath>

namespace jaudios
{

OfflineRenderReport renderThroughEngine (AudioEngine& engine, std::shared_ptr<const juce::AudioBuffer<float>> source,
                                         double sampleRate, int blockSize, std::vector<int> deviceBlockSizes)
{
    if (deviceBlockSizes.empty())
        deviceBlockSizes.push_back (blockSize);
    const int maxDeviceBlock = *std::max_element (deviceBlockSizes.begin(), deviceBlockSizes.end());

    OfflineRenderReport report;
    report.blockSize = blockSize;

    engine.prepareOffline (sampleRate, blockSize);

    const int length = source->getNumSamples();
    auto& player = engine.getSourcePlayer();
    player.setSource (source);
    player.play();

    report.output.setSize (2, length);
    report.output.clear();

    // Allocated before the realtime loop, exactly like device channel pointers.
    juce::AudioBuffer<float> deviceBuffer (2, maxDeviceBlock);

    rt::resetStats();

    size_t callback = 0;
    for (int pos = 0; pos < length;)
    {
        const int n = std::min (deviceBlockSizes[callback++ % deviceBlockSizes.size()], length - pos);
        engine.renderBlock (deviceBuffer.getArrayOfWritePointers(), 2, n);

        for (int ch = 0; ch < 2; ++ch)
            report.output.copyFrom (ch, pos, deviceBuffer, ch, 0, n);

        ++report.numBlocks;
        pos += n;
    }

    report.rtStats = rt::getStats();
    report.latencyMs = engine.getOutputLatencyMs();

    const int srcChannels = source->getNumChannels();
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto* out = report.output.getReadPointer (ch);
        const auto* in = source->getReadPointer (std::min (ch, srcChannels - 1));
        for (int i = 0; i < length; ++i)
            report.maxAbsDiffVsSource = std::max (report.maxAbsDiffVsSource, std::abs (out[i] - in[i]));
    }

    player.stop();
    player.setSource (nullptr);
    engine.renderBlock (deviceBuffer.getArrayOfWritePointers(), 2, 1); // let the player retire the buffer
    player.collectGarbage();

    return report;
}

} // namespace jaudios
