#pragma once

#include "SpscQueue.h"

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <atomic>
#include <memory>
#include <vector>

namespace jaudios
{

/** Boilerplate-free base for our internal graph nodes (no editor, no programs, no state). */
class EngineProcessorBase : public juce::AudioProcessor
{
public:
    using juce::AudioProcessor::AudioProcessor;

    void releaseResources() override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

protected:
    static BusesProperties stereoOutOnly()
    {
        return BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true);
    }

    static BusesProperties stereoInOut()
    {
        return BusesProperties()
            .withInput ("In", juce::AudioChannelSet::stereo(), true)
            .withOutput ("Out", juce::AudioChannelSet::stereo(), true);
    }
};

//==============================================================================
/** Plays an in-memory buffer (the imported/captured source, later: stems).

    Ownership protocol (no allocation or freeing on the audio thread):
      - The message thread keeps every buffer alive in `owned` and sends a raw pointer to the
        audio thread through a lock-free command queue.
      - When the audio thread swaps a buffer out, it sends the old pointer back through the
        `retired` queue; `collectGarbage()` (message thread) then drops the shared_ptr.
*/
class SourcePlayerProcessor final : public EngineProcessorBase
{
public:
    using Buffer = juce::AudioBuffer<float>;

    SourcePlayerProcessor();

    const juce::String getName() const override { return "Source"; }

    // --- message thread -------------------------------------------------------------------
    bool setSource (std::shared_ptr<const Buffer> buffer);
    bool play();
    bool stop();
    bool seek (juce::int64 samplePosition);
    void collectGarbage();

    juce::int64 getPosition() const noexcept { return positionForUi.load (std::memory_order_relaxed); }
    bool isPlaying() const noexcept { return playingForUi.load (std::memory_order_relaxed); }

    // --- audio thread ---------------------------------------------------------------------
    void prepareToPlay (double, int) override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    struct Command
    {
        enum class Type { setSource, play, stop, seek };
        Type type = Type::stop;
        const Buffer* buffer = nullptr;
        juce::int64 position = 0;
    };

    void handleCommands() noexcept;

    SpscQueue<Command, 64> commands;
    SpscQueue<const Buffer*, 64> retired;

    std::vector<std::shared_ptr<const Buffer>> owned; // message thread only

    const Buffer* current = nullptr; // audio thread only
    juce::int64 readPosition = 0;    // audio thread only
    bool playing = false;            // audio thread only

    std::atomic<juce::int64> positionForUi { 0 };
    std::atomic<bool> playingForUi { false };
};

//==============================================================================
/** Per-channel gain / mute (solo is resolved by AudioEngine into an effective mute). */
class ChannelStripProcessor final : public EngineProcessorBase
{
public:
    explicit ChannelStripProcessor (juce::String name);

    const juce::String getName() const override { return name; }

    void setGainDecibels (float dB) noexcept;
    void setEffectiveMute (bool shouldMute) noexcept { muted.store (shouldMute, std::memory_order_relaxed); }

    void prepareToPlay (double sampleRate, int maxBlockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    juce::String name;
    std::atomic<float> gainLinear { 1.0f };
    std::atomic<bool> muted { false };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoother;
};

} // namespace jaudios
