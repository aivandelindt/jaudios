#pragma once

#include "EngineProcessors.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <array>

namespace jaudios
{

/** Mixer channels. M1 only wires `source`; chords/bass/drums nodes land with their generators. */
enum class ChannelId : int { source = 0, chords, bass, drums };
inline constexpr int numChannelIds = 4;

/** Owns the audio device and the processing graph.

    Graph (M1):   [SourcePlayer] -> [Strip: source] -> [Master] -> [Audio out]
    Later:        [Chord synth] / [Bass synth] / [Drums] -> [Strip] -> FX -> [Master]

    Threading:
      - message thread: device setup, transport commands, gain/mute/solo (all lock-free into RT)
      - audio thread:   renderBlock() only. No allocation, no locks of our own, no logging.
*/
class AudioEngine final : private juce::AudioIODeviceCallback
{
public:
    static constexpr int defaultBlockSize = 128;

    AudioEngine();
    ~AudioEngine() override;

    // --- device (message thread) ----------------------------------------------------------
    /** Opens the default output device at the requested buffer size. Returns an error string
        (empty on success). */
    juce::String openDefaultDevice (int preferredBufferSize = defaultBlockSize);
    void closeDevice();
    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    // --- offline / headless ---------------------------------------------------------------
    /** Prepare for rendering without a device (CLI, tests, offline export). */
    void prepareOffline (double sampleRate, int blockSize);

    // --- audio thread ---------------------------------------------------------------------
    /** Renders numSamples into the output channels. Realtime-safe once prepared. */
    void renderBlock (float* const* outputs, int numOutputs, int numSamples) noexcept;

    // --- mixer / transport (message thread) -----------------------------------------------
    SourcePlayerProcessor& getSourcePlayer() noexcept { return *sourcePlayer; }
    void setChannelGainDecibels (ChannelId, float dB);
    void setChannelMute (ChannelId, bool);
    void setChannelSolo (ChannelId, bool);
    void setMasterGainDecibels (float dB);

    // --- info -----------------------------------------------------------------------------
    double getSampleRate() const noexcept { return preparedSampleRate; }
    int getBlockSize() const noexcept { return preparedBlockSize; }
    /** Output latency in ms: device buffer + device-reported output latency + graph latency. */
    double getOutputLatencyMs() const;

private:
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    void prepare (double sampleRate, int blockSize);
    void renderGraphBlock() noexcept;
    void updateEffectiveMutes();

    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorGraph graph;

    SourcePlayerProcessor* sourcePlayer = nullptr; // owned by graph
    std::array<ChannelStripProcessor*, numChannelIds> strips {};
    ChannelStripProcessor* master = nullptr;

    std::array<bool, numChannelIds> mutes {};
    std::array<bool, numChannelIds> solos {};

    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midiScratch;

    int pendingSamples = 0; // rendered samples in `scratch` not yet delivered (audio thread)

    double preparedSampleRate = 0.0;
    int preparedBlockSize = 0;
    bool prepared = false;
};

} // namespace jaudios
