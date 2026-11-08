#pragma once

#include "SpscQueue.h"

#include "engine/fx/FxChain.h"
#include "engine/synth/Instrument.h"

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
/** Plays an in-memory buffer (the imported/captured source) and, once separated, its stems, all
    on one transport so they stay sample-aligned.

    Output: 10 channels = 5 stereo layers. Channels 0-1: the source; 2+2s / 3+2s: stem s in model
    order (drums, bass, other, vocals). The stems buffer has 8 channels (stem s, channel c ->
    channel 2s+c) and the source's length; layers without data output silence.

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

    static constexpr int numStemLayers = 4;
    static constexpr int numOutputChannels = 2 + 2 * numStemLayers;

    // --- message thread -------------------------------------------------------------------
    /** New source: also drops the current stems (they belonged to the old source). */
    bool setSource (std::shared_ptr<const Buffer> buffer);
    /** Stems of the current source (8 channels, see above), or nullptr to remove them. Keeps the
        transport position. */
    bool setStems (std::shared_ptr<const Buffer> stems);
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
        enum class Type { setSource, setStems, play, stop, seek };
        Type type = Type::stop;
        const Buffer* buffer = nullptr;
        juce::int64 position = 0;
    };

    void handleCommands() noexcept;
    bool keep (std::shared_ptr<const Buffer>);
    void retire (const Buffer*& slot) noexcept;

    SpscQueue<Command, 64> commands;
    SpscQueue<const Buffer*, 64> retired;

    std::vector<std::shared_ptr<const Buffer>> owned; // message thread only

    const Buffer* current = nullptr; // audio thread only
    const Buffer* stems = nullptr;   // audio thread only
    juce::int64 readPosition = 0;    // audio thread only
    bool playing = false;            // audio thread only

    std::atomic<juce::int64> positionForUi { 0 };
    std::atomic<bool> playingForUi { false };
};

//==============================================================================
/** A generated part (chords, bass, drums) as MIDI events on the transport's sample timeline (the
    source buffer's samples), sorted by position. */
struct ClipEvent
{
    juce::int64 position = 0;
    juce::MidiMessage message;
};
using MidiClip = std::vector<ClipEvent>;

/** Seconds-stamped MIDI (generators, transcription) -> a clip at `transportRate` samples/second. */
MidiClip makeClip (const juce::MidiMessageSequence&, double transportRate);

/** Plays a MidiClip through an Instrument on the same transport as the SourcePlayer (play / stop /
    seek commands reach both from AudioEngine, so they advance block for block in step).
    Clip and instrument swap with the SourcePlayer's ownership protocol: the message thread keeps
    them alive and sends raw pointers through a lock-free queue; retired ones come back to be freed
    by collectGarbage(). An instrument must be prepared before it is sent (AudioEngine does that). */
class ClipPlayerProcessor final : public EngineProcessorBase
{
public:
    explicit ClipPlayerProcessor (juce::String name);
    const juce::String getName() const override { return name; }

    // --- message thread -------------------------------------------------------------------
    bool setClip (std::shared_ptr<const MidiClip>);
    bool setInstrument (std::shared_ptr<synth::Instrument>);
    bool play();
    bool stop();
    bool seek (juce::int64 position);
    /** The source's length: like the SourcePlayer, playback stops when the transport reaches it
        (same block arithmetic, so both stop on the same sample). -1 = no limit (no source). */
    bool setLength (juce::int64 length);
    void collectGarbage();
    /** Output channel 2 carries a trigger (1.0 on the sample) for every note-on of
        `note` in the clip (the drums player: GM kick 36, the ducker's "kick events" key); -1 = none. */
    void setTriggerNote (int note) noexcept { triggerNote.store (note, std::memory_order_relaxed); }
    /** Instruments kept alive here (prepareToPlay re-prepares them for a new device setup). */
    const std::vector<std::shared_ptr<synth::Instrument>>& instruments() const noexcept { return ownedInstruments; }

    // --- audio thread ---------------------------------------------------------------------
    void prepareToPlay (double sampleRate, int maxBlockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    struct Command
    {
        enum class Type { setClip, setInstrument, play, stop, seek, setLength };
        Type type = Type::stop;
        const MidiClip* clip = nullptr;
        synth::Instrument* instrument = nullptr;
        juce::int64 position = 0;
    };
    struct Retired { const MidiClip* clip = nullptr; synth::Instrument* instrument = nullptr; };

    void handleCommands() noexcept;
    void relocate() noexcept; // cursor = first event at or after readPosition

    juce::String name;
    SpscQueue<Command, 64> commands;
    SpscQueue<Retired, 64> retired;
    std::vector<std::shared_ptr<const MidiClip>> ownedClips;            // message thread only
    std::vector<std::shared_ptr<synth::Instrument>> ownedInstruments;   // message thread only

    const MidiClip* clip = nullptr;            // audio thread only
    synth::Instrument* instrument = nullptr;   // audio thread only
    juce::int64 readPosition = 0;
    juce::int64 length = -1;
    size_t cursor = 0;
    bool playing = false;
    juce::MidiBuffer blockMidi;                // reserved in prepareToPlay
    std::atomic<int> triggerNote { -1 };
};

//==============================================================================
/** Per-channel insert FX (fx-chain) then gain / mute (solo is resolved by AudioEngine into an
    effective mute). Inputs: 0-1 the channel, 2-4 the ducker's key (2: kick triggers from the drums
    clip player, 3-4: the drums stem). The FX parameters live in this strip's
    AudioProcessorValueTreeState (message thread writes, audio thread reads, every one smoothed). */
class ChannelStripProcessor final : public EngineProcessorBase
{
public:
    explicit ChannelStripProcessor (juce::String name);

    const juce::String getName() const override { return name; }

    void setGainDecibels (float dB) noexcept;
    void setEffectiveMute (bool shouldMute) noexcept { muted.store (shouldMute, std::memory_order_relaxed); }
    juce::AudioProcessorValueTreeState& fxState() noexcept { return fx; }
    void setTempo (double bpm) noexcept { fxChain.setTempo (bpm); }

    void prepareToPlay (double sampleRate, int maxBlockSize) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    /** The FX settings (APVTS state as XML). */
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    juce::String name;
    juce::AudioProcessorValueTreeState fx;
    fx::FxChain fxChain;
    std::atomic<float> gainLinear { 1.0f };
    std::atomic<bool> muted { false };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoother;
};

} // namespace jaudios
