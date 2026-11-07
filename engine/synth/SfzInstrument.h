#pragma once

#include "DrumSampler.h"
#include "Instrument.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sfz { class Sfizz; }

namespace jaudios::synth
{

/** A built-in SFZ preset (engine/synth/presets/<id>.sfz, embedded at build time). All of them are
    JAudios' own patches made from sfizz's oscillators (*sine, *saw, ...): no sample files. */
struct SfzPreset
{
    std::string id;        // file name without .sfz, e.g. "pad"
    std::string name;      // "// name:" header
    std::string category;  // "// category:" header: keys, pad, pluck, bass
    std::string sfz;       // the SFZ text
};

const std::vector<SfzPreset>& builtinSfzPresets();
const SfzPreset* findSfzPreset (const std::string& id);
/** Categories in first-seen order (preset browser). */
std::vector<std::string> sfzCategories();

/** sfizz (BSD-2) playing one SFZ instrument. load*() on a worker / message thread; process() on
    the audio thread (sfizz renders without allocating once loaded and prepared). */
class SfzInstrument final : public Instrument
{
public:
    SfzInstrument();
    ~SfzInstrument() override;

    bool load (const SfzPreset&);
    bool loadFile (const juce::File& sfzFile);
    int numRegions() const noexcept;
    /** What was loaded: a built-in preset id, or the SFZ file (for project files). */
    const std::string& presetId() const noexcept { return loadedPreset; }
    const juce::File& sfzFile() const noexcept { return loadedFile; }

    juce::String name() const override { return displayName; }
    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) noexcept override;
    void allNotesOff() noexcept override;

private:
    std::unique_ptr<sfz::Sfizz> synth;
    juce::String displayName;
    std::string loadedPreset;
    juce::File loadedFile;
};

/** The drum sampler as an Instrument (drums channel). The kit is (re)built by `makeKit` for the
    sample rate prepare() gets (the built-in kit by default; a user folder via DrumKit::loadFolder). */
class DrumInstrument final : public Instrument
{
public:
    explicit DrumInstrument (std::function<DrumKit (double sampleRate)> makeKit = &DrumKit::synthesised);
    juce::String name() const override { return "Drums (" + kit.name + ")"; }
    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) noexcept override;
    void allNotesOff() noexcept override;

private:
    std::function<DrumKit (double)> makeKit;
    DrumKit kit;
    std::unique_ptr<DrumSampler> sampler;
};

} // namespace jaudios::synth
