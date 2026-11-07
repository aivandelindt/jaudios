#include "EngineProcessors.h"

#include <algorithm>

namespace jaudios
{

//==============================================================================
SourcePlayerProcessor::SourcePlayerProcessor()
    : EngineProcessorBase (BusesProperties().withOutput ("Out", juce::AudioChannelSet::discreteChannels (numOutputChannels), true))
{
    owned.reserve (16);
}

bool SourcePlayerProcessor::setSource (std::shared_ptr<const Buffer> buffer)
{
    collectGarbage();
    const auto* raw = buffer.get();
    if (! commands.push ({ Command::Type::setSource, raw, 0 }))
        return false;
    return keep (std::move (buffer));
}

bool SourcePlayerProcessor::setStems (std::shared_ptr<const Buffer> buffer)
{
    jassert (buffer == nullptr || buffer->getNumChannels() == 2 * numStemLayers);
    collectGarbage();
    const auto* raw = buffer.get();
    if (! commands.push ({ Command::Type::setStems, raw, 0 }))
        return false;
    return keep (std::move (buffer));
}

bool SourcePlayerProcessor::keep (std::shared_ptr<const Buffer> buffer)
{
    if (buffer != nullptr)
        owned.push_back (std::move (buffer));
    return true;
}

void SourcePlayerProcessor::retire (const Buffer*& slot) noexcept
{
    if (slot != nullptr)
        retired.push (slot); // queue sized so this cannot overflow in practice
    slot = nullptr;
}

bool SourcePlayerProcessor::play() { return commands.push ({ Command::Type::play, nullptr, 0 }); }
bool SourcePlayerProcessor::stop() { return commands.push ({ Command::Type::stop, nullptr, 0 }); }
bool SourcePlayerProcessor::seek (juce::int64 pos) { return commands.push ({ Command::Type::seek, nullptr, pos }); }

void SourcePlayerProcessor::collectGarbage()
{
    const Buffer* old = nullptr;
    while (retired.pop (old))
    {
        auto it = std::find_if (owned.begin(), owned.end(), [old] (const auto& p) { return p.get() == old; });
        if (it != owned.end())
            owned.erase (it);
    }
}

void SourcePlayerProcessor::handleCommands() noexcept
{
    Command cmd;
    while (commands.pop (cmd))
    {
        switch (cmd.type)
        {
            case Command::Type::setSource:
                retire (current);
                retire (stems);
                current = cmd.buffer;
                readPosition = 0;
                playing = false;
                break;
            case Command::Type::setStems:
                retire (stems);
                stems = cmd.buffer;
                break;
            case Command::Type::play:  playing = current != nullptr; break;
            case Command::Type::stop:  playing = false; break;
            case Command::Type::seek:  readPosition = std::max<juce::int64> (0, cmd.position); break;
        }
    }
}

void SourcePlayerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    handleCommands();
    buffer.clear();

    const int numSamples = buffer.getNumSamples();

    if (playing && current != nullptr)
    {
        const auto length = (juce::int64) current->getNumSamples();
        const int n = (int) std::clamp<juce::int64> (length - readPosition, 0, numSamples);
        const int srcChannels = current->getNumChannels();

        for (int ch = 0; ch < 2 && ch < buffer.getNumChannels() && n > 0; ++ch)
            buffer.copyFrom (ch, 0, *current, std::min (ch, srcChannels - 1), (int) readPosition, n);

        if (stems != nullptr)
        {
            const int m = (int) std::clamp<juce::int64> ((juce::int64) stems->getNumSamples() - readPosition, 0, n);
            for (int ch = 0; ch < 2 * numStemLayers && 2 + ch < buffer.getNumChannels() && m > 0; ++ch)
                buffer.copyFrom (2 + ch, 0, *stems, ch, (int) readPosition, m);
        }

        readPosition += n;
        if (readPosition >= length)
            playing = false;
    }

    positionForUi.store (readPosition, std::memory_order_relaxed);
    playingForUi.store (playing, std::memory_order_relaxed);
}

//==============================================================================
ChannelStripProcessor::ChannelStripProcessor (juce::String n)
    : EngineProcessorBase (stereoInOut()), name (std::move (n))
{
}

void ChannelStripProcessor::setGainDecibels (float dB) noexcept
{
    gainLinear.store (juce::Decibels::decibelsToGain (dB, -100.0f), std::memory_order_relaxed);
}

void ChannelStripProcessor::prepareToPlay (double sampleRate, int)
{
    smoother.reset (sampleRate, 0.02);
    smoother.setCurrentAndTargetValue (muted.load() ? 0.0f : gainLinear.load());
}

void ChannelStripProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    smoother.setTargetValue (muted.load (std::memory_order_relaxed) ? 0.0f
                                                                     : gainLinear.load (std::memory_order_relaxed));

    if (! smoother.isSmoothing())
    {
        buffer.applyGain (smoother.getTargetValue());
        return;
    }

    const int numChannels = buffer.getNumChannels();
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float g = smoother.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }
}

} // namespace jaudios
