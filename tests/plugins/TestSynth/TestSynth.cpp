// JAudios Test Synth: a tiny VST3 instrument built in-repo, so CI can test VST3 hosting end to end
// (scan out of process, load, play generated chords, save / restore state) without third-party
// plugins. Sine voices; parameters "gain" and "octave" are the state the tests round-trip.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace
{
struct SineSound final : juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

struct SineVoice final : juce::SynthesiserVoice
{
    explicit SineVoice (std::atomic<float>& octaveParam) : octave (octaveParam) {}
    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<SineSound*> (s) != nullptr; }
    void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
    {
        const double hz = juce::MidiMessage::getMidiNoteInHertz (note + 12 * (int) octave.load());
        delta = juce::MathConstants<double>::twoPi * hz / getSampleRate();
        phase = 0.0;
        level = 0.25 * velocity;
        tail = -1.0;
    }
    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
            tail = 1.0;
        else
        {
            clearCurrentNote();
            delta = 0.0;
        }
    }
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (delta == 0.0)
            return;
        while (--num >= 0)
        {
            double v = std::sin (phase) * level;
            if (tail > 0.0)
            {
                v *= tail;
                tail *= 0.995;
                if (tail < 0.001)
                {
                    clearCurrentNote();
                    delta = 0.0;
                    return;
                }
            }
            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.addSample (ch, start, (float) v);
            phase += delta;
            ++start;
        }
    }
    std::atomic<float>& octave;
    double phase = 0.0, delta = 0.0, level = 0.0, tail = -1.0;
};
} // namespace

class TestSynthProcessor final : public juce::AudioProcessor
{
public:
    TestSynthProcessor()
        : AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, "JAudiosTestSynth",
                 { std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "gain", 1 }, "Gain", 0.0f, 1.0f, 0.8f),
                   std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "octave", 1 }, "Octave", -2, 2, 0) })
    {
        for (int i = 0; i < 16; ++i)
            synth.addVoice (new SineVoice (*state.getRawParameterValue ("octave")));
        synth.addSound (new SineSound());
    }

    const juce::String getName() const override { return "JAudios Test Synth"; }
    void prepareToPlay (double sr, int) override { synth.setCurrentPlaybackSampleRate (sr); }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        buffer.clear();
        synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
        buffer.applyGain (state.getRawParameterValue ("gain")->load());
    }
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    double getTailLengthSeconds() const override { return 0.5; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (auto xml = state.copyState().createXml())
            copyXmlToBinary (*xml, dest);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            state.replaceState (juce::ValueTree::fromXml (*xml));
    }

private:
    juce::AudioProcessorValueTreeState state;
    juce::Synthesiser synth;
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestSynthProcessor(); }
