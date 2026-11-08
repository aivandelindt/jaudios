#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <vector>

namespace jaudios::fx
{

/** Every insert-FX parameter of a channel (fx-chain skill). The order is the processing order of
    the stages: filter -> saturation -> OTT -> ducker -> delay -> reverb. */
enum class Param : int
{
    filterOn, filterType, filterCutoff, filterResonance,
    satOn, satDrive, satMix,
    ottOn, ottDepth, ottTime, ottInGain, ottOutGain,
    duckOn, duckSource, duckDepth, duckRelease,
    delayOn, delayDivision, delayFeedback, delayMix,
    reverbOn, reverbSize, reverbDamping, reverbWidth, reverbMix,
    count
};
inline constexpr int numParams = (int) Param::count;

enum class FilterType : int { lowPass = 0, highPass, bandPass };
enum class DuckSource : int { kickEvents = 0, drumsStem };

struct ParamSpec
{
    enum class Kind { toggle, choice, range };
    Param param;
    const char* id;        // "filter_cutoff"
    const char* name;      // "Filter cutoff"
    Kind kind;
    float min, max, defaultValue;
    float skewCentre;      // range: value at the middle of the control (0 = linear)
    const char* unit;      // "Hz", "dB", "ms", ""
    std::vector<const char*> choices;
};

const std::array<ParamSpec, numParams>& paramSpecs();
const ParamSpec& spec (Param);
const ParamSpec* findSpec (juce::StringRef id);

/** Tempo-synced delay divisions (choices of delay_division), in beats. */
inline constexpr std::array<float, 6> delayDivisionBeats { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };

/** The layout every channel strip's AudioProcessorValueTreeState is built from. */
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Sets a parameter by id to a plain value (Hz, dB, ms, 0/1 for toggles, the index or the name
    of a choice), as a host would (setValueNotifyingHost). Message thread. */
bool setParameter (juce::AudioProcessorValueTreeState&, juce::StringRef id, float plainValue);
bool setParameter (juce::AudioProcessorValueTreeState&, juce::StringRef id, const juce::String& plainValueOrChoice);

/** A parameter tree without a channel strip around it: offline tools and tests. */
class ParameterHost final : public juce::AudioProcessor
{
public:
    ParameterHost();
    juce::AudioProcessorValueTreeState& state() noexcept { return apvts; }

    const juce::String getName() const override { return "FX"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
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

private:
    juce::AudioProcessorValueTreeState apvts;
};

/** The sidechain key for the ducker: per-sample kick triggers (1 on the sample a kick note starts,
    0 elsewhere: a trigger ducks by the full depth) and / or the drums stem. Null = no key. */
struct SidechainKey
{
    const float* kickTriggers = nullptr;
    const float* stemLeft = nullptr;
    const float* stemRight = nullptr;
};

/** One channel's insert chain. Parameters are read from the bound tree's raw values once per
    block and every continuous one is smoothed per sample; switching a stage on or off crossfades
    over 10 ms, a filter-type change crossfades its outputs, and a delay-time change crossfades
    between the old and the new tap (no zipper noise, no pitch glide). A stage that is off and
    faded out costs nothing. prepare() allocates; process() never does (audio thread). */
class FxChain
{
public:
    FxChain();
    void bind (juce::AudioProcessorValueTreeState&);    // before prepare; message thread
    void prepare (double sampleRate, int maxBlockSize);
    void setTempo (double bpm) noexcept;                 // any thread
    void process (float* const* channels, int numChannels, int numSamples, const SidechainKey& = {}) noexcept;
    /** Any stage on, or still fading out (a strip may skip process() otherwise). */
    bool isActive() const noexcept;

private:
    float raw (Param p) const noexcept { return values[(size_t) p] != nullptr ? values[(size_t) p]->load (std::memory_order_relaxed) : spec (p).defaultValue; }
    bool on (Param p) const noexcept { return raw (p) >= 0.5f; }

    using Linear = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;
    using Multiplicative = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;

    /** A stage's on/off crossfade. Returns false when the stage is fully off (skip it). */
    struct Switch
    {
        Linear amount;
        bool update (bool shouldBeOn, bool& justStarted) noexcept;
    };

    void filter (float* const* io, int numChannels, int n) noexcept;
    void saturation (float* const* io, int numChannels, int n) noexcept;
    void ott (float* const* io, int numChannels, int n) noexcept;
    void ducker (float* const* io, int numChannels, int n, const SidechainKey&) noexcept;
    void delay (float* const* io, int numChannels, int n) noexcept;
    void reverb (float* const* io, int numChannels, int n) noexcept;

    std::array<std::atomic<float>*, numParams> values {};
    std::atomic<float> tempo { 120.0f };
    double sampleRate = 44100.0;
    int maxBlock = 0;

    // filter: TPT state-variable (low / band / high computed together, so the type crossfades)
    Switch filterSwitch;
    Multiplicative cutoff;
    Linear resonance;
    std::array<Linear, 3> typeWeights;
    std::array<float, 2> ic1 {}, ic2 {};

    // saturation
    Switch satSwitch;
    Multiplicative drive;
    Linear satMix;

    // OTT: 3 bands (88.3 Hz / 2.5 kHz Linkwitz-Riley), upward + downward per band, stereo-linked
    Switch ottSwitch;
    Linear ottDepth, ottTime;
    Multiplicative ottIn, ottOut;
    juce::dsp::LinkwitzRileyFilter<float> crossLow, crossHigh, lowAllpass;
    std::array<float, 3> ottEnv {};
    int ottWarmup = 0;

    // ducker
    Switch duckSwitch;
    Linear duckDepth, duckRelease;
    float duckEnv = 0.0f, duckGain = 1.0f;

    // ping-pong delay (tempo-synced; time changes crossfade between two taps)
    Switch delaySwitch;
    Linear feedback, delayMix;
    juce::AudioBuffer<float> delayLines;
    int writePos = 0, delayNow = 0, delayNext = 0;
    float tapFade = 1.0f;

    // reverb (JUCE's Freeverb; its damping, size and gains are smoothed inside)
    Switch reverbSwitch;
    Linear reverbMix;
    juce::Reverb reverbEngine;
    juce::Reverb::Parameters reverbParams;
    juce::AudioBuffer<float> scratch;
};

} // namespace jaudios::fx
