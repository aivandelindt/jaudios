#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace jaudios::synth
{

/** A sound source on a generated channel (chords, bass, drums): built-in SFZ, the drum sampler, or
    a hosted plugin (synth-and-vst3-host). Loading and prepare() happen off the audio thread;
    process() and allNotesOff() run on it and must not allocate or lock. */
class Instrument
{
public:
    virtual ~Instrument() = default;

    virtual juce::String name() const = 0;
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    /** Renders `out.getNumSamples()` samples into `out` (stereo, overwritten) for the MIDI events
        in `midi` (sample positions within this block). */
    virtual void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) noexcept = 0;
    virtual void allNotesOff() noexcept = 0;
};

} // namespace jaudios::synth
