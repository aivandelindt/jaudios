#pragma once

#include "EngineProcessors.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <array>

namespace jaudios
{

/** Mixer channels: `source`, the generated parts (chords, bass, drums: ClipPlayer + instrument) and
    the four stems (model order, see stemChannel()). */
enum class ChannelId : int { source = 0, chords, bass, drums, stemDrums, stemBass, stemOther, stemVocals };
inline constexpr int numChannelIds = 8;

/** Stem index in model order (0 drums, 1 bass, 2 other, 3 vocals) -> its mixer channel. */
constexpr ChannelId stemChannel (int stemIndex) noexcept { return (ChannelId) ((int) ChannelId::stemDrums + stemIndex); }

/** Owns the audio device and the processing graph.

    Graph:        [SourcePlayer] ch 0-1 -> [Strip: source]      -> [Master] -> [Audio out]
                                 ch 2-9 -> [Strip: stem s] x 4  -> [Master]
                  [ClipPlayer: chords / bass / drums] -> [Strip] -> [Master]   (FX inserts: fx-chain)

    While stems are loaded the source strip is muted (the stems replace it) unless it is soloed.
    play() / stop() / seek() drive the source and every clip player together (one transport).
    Bypass mutes everything generated (and the stems) and passes the source through; mutes and
    solos are left as they were, so leaving bypass restores the previous state.

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
    void play();
    void stop();
    void seek (juce::int64 transportSample);

    /** A generated part on `channel` (chords, bass or drums): the clip to play, and its instrument
        (prepared here, at the engine's rate, before it reaches the audio thread). */
    bool setChannelClip (ChannelId, std::shared_ptr<const MidiClip>);
    bool setChannelInstrument (ChannelId, std::shared_ptr<synth::Instrument>);
    /** Bypass: mute every generated channel and the stems, pass the source through. */
    void setBypass (bool);
    bool isBypassed() const noexcept { return bypassed; }
    /** Stems of the current source (8 channels: stem s, channel c -> 2s+c; same length), or
        nullptr to go back to the source. */
    bool setStems (std::shared_ptr<const juce::AudioBuffer<float>> stems);
    /** New source (drops its stems). Use this rather than getSourcePlayer().setSource(). */
    bool setSource (std::shared_ptr<const juce::AudioBuffer<float>> source);
    bool hasStems() const noexcept { return stemsLoaded; }
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
    ClipPlayerProcessor* clipPlayer (ChannelId) const noexcept;
    std::array<ClipPlayerProcessor*, 3> clipPlayers {}; // chords, bass, drums (owned by graph)
    bool bypassed = false;
    std::array<ChannelStripProcessor*, numChannelIds> strips {};
    ChannelStripProcessor* master = nullptr;

    std::array<bool, numChannelIds> mutes {};
    std::array<bool, numChannelIds> solos {};
    bool stemsLoaded = false;

    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midiScratch;

    int pendingSamples = 0; // rendered samples in `scratch` not yet delivered (audio thread)

    double preparedSampleRate = 0.0;
    int preparedBlockSize = 0;
    bool prepared = false;
};

} // namespace jaudios
