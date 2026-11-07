#pragma once

#include "engine/generators/DrumGenerator.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <optional>

namespace jaudios::synth
{

using generators::DrumElement;
using generators::numDrumElements;

/** Samples per element: [velocity layer (soft .. hard)][round robin], mono, at `sampleRate`. */
struct DrumKit
{
    double sampleRate = 44100.0;
    juce::String name;
    std::array<std::vector<std::vector<juce::AudioBuffer<float>>>, numDrumElements> layers;

    /** JAudios' own kit, synthesised (no third-party samples): 3 velocity layers x 3 round robins
        per element; harder layers are brighter, round robins differ in noise and +-1.5 % pitch. */
    static DrumKit synthesised (double sampleRate);

    /** A folder of WAV/AIFF/FLAC files named <element>[_v<layer>][_rr<n>].<ext> (e.g. kick.wav,
        snare_v2_rr3.wav), resampled to `sampleRate`. Elements without files come from the
        synthesised kit. nullopt (with `error`) when the folder has no usable file at all. */
    static std::optional<DrumKit> loadFolder (const juce::File&, double sampleRate, juce::String& error);
};

/** juce::Synthesiser playing a DrumKit on the GM drum notes: one-shot voices, round robin per
    element, velocity layers by velocity (thirds), a closed hat chokes the open hat, a fixed pan per
    element. renderNextBlock allocates nothing once prepared (voices are created up front).
    The kit is referenced, not copied: it must outlive the sampler. */
class DrumSampler final : public juce::Synthesiser
{
public:
    explicit DrumSampler (const DrumKit&, int numVoices = 24);
    void noteOn (int midiChannel, int midiNoteNumber, float velocity) override;
    /** Allocation-free voice stealing (juce::Synthesiser's findVoiceToSteal clears and refills an
        Array, which reallocates on the audio thread): a busy element retriggers its own oldest
        voice, otherwise the oldest voice overall is reused. */
    juce::SynthesiserVoice* findFreeVoice (juce::SynthesiserSound*, int midiChannel, int midiNoteNumber, bool stealIfNoneAvailable) const override;
    /** Mute every element but `only` (offline per-element export). */
    void solo (std::optional<DrumElement> only) noexcept { soloElement = only; }

private:
    std::optional<DrumElement> soloElement;
};

/** Renders drum notes offline through DrumSampler (blocks of 512 samples, sample-accurate note
    starts): stereo, `numSamples` long. `only` renders a single element. */
juce::AudioBuffer<float> renderDrums (const std::vector<generators::DrumNote>&, const DrumKit&, double sampleRate, int numSamples,
                                      std::optional<DrumElement> only = std::nullopt);

/** The notes as a MIDI sequence on channel 10 (GM drums), timestamps in seconds. */
juce::MidiMessageSequence drumsToMidi (const std::vector<generators::DrumNote>&, std::optional<DrumElement> only = std::nullopt);

} // namespace jaudios::synth
