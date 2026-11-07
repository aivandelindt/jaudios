#include "SfzInstrument.h"

#include <sfizz.hpp>

namespace jaudios::synth
{

// Generated from engine/synth/presets/*.sfz by engine/CMakeLists.txt.
extern const char* const embeddedSfzPresetIds[];
extern const char* const embeddedSfzPresets[];
extern const int numEmbeddedSfzPresets;

namespace
{
std::string header (const std::string& sfz, const char* key)
{
    const std::string tag = std::string ("// ") + key + ":";
    const auto at = sfz.find (tag);
    if (at == std::string::npos)
        return {};
    const auto end = sfz.find ('\n', at);
    return juce::String (sfz.substr (at + tag.size(), end - at - tag.size())).trim().toStdString();
}
} // namespace

const std::vector<SfzPreset>& builtinSfzPresets()
{
    static const std::vector<SfzPreset> presets = [] {
        std::vector<SfzPreset> out;
        for (int i = 0; i < numEmbeddedSfzPresets; ++i)
        {
            SfzPreset p;
            p.id = embeddedSfzPresetIds[i];
            p.sfz = embeddedSfzPresets[i];
            p.name = header (p.sfz, "name");
            p.category = header (p.sfz, "category");
            out.push_back (std::move (p));
        }
        return out;
    }();
    return presets;
}

const SfzPreset* findSfzPreset (const std::string& id)
{
    for (const auto& p : builtinSfzPresets())
        if (p.id == id)
            return &p;
    return nullptr;
}

std::vector<std::string> sfzCategories()
{
    std::vector<std::string> cats;
    for (const auto& p : builtinSfzPresets())
        if (std::find (cats.begin(), cats.end(), p.category) == cats.end())
            cats.push_back (p.category);
    return cats;
}

//==============================================================================
SfzInstrument::SfzInstrument() : synth (std::make_unique<sfz::Sfizz>()) {}
SfzInstrument::~SfzInstrument() = default;

bool SfzInstrument::load (const SfzPreset& p)
{
    displayName = p.name.empty() ? juce::String (p.id) : juce::String (p.name);
    // a virtual path: the presets reference no files (oscillators only)
    return synth->loadSfzString ("/jaudios/presets/" + p.id + ".sfz", p.sfz) && synth->getNumRegions() > 0;
}

bool SfzInstrument::loadFile (const juce::File& f)
{
    displayName = f.getFileNameWithoutExtension();
    return synth->loadSfzFile (f.getFullPathName().toStdString()) && synth->getNumRegions() > 0;
}

int SfzInstrument::numRegions() const noexcept { return synth->getNumRegions(); }

void SfzInstrument::prepare (double sampleRate, int maxBlockSize)
{
    synth->setSampleRate ((float) sampleRate);
    synth->setSamplesPerBlock (maxBlockSize);
}

void SfzInstrument::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) noexcept
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int at = meta.samplePosition;
        if (m.isNoteOn())
            synth->noteOn (at, m.getNoteNumber(), m.getVelocity());
        else if (m.isNoteOff())
            synth->noteOff (at, m.getNoteNumber(), m.getVelocity());
        else if (m.isPitchWheel())
            synth->pitchWheel (at, m.getPitchWheelValue() - 8192);
        else if (m.isController())
            synth->cc (at, m.getControllerNumber(), m.getControllerValue());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            synth->allSoundOff();
    }
    float* channels[2] = { out.getWritePointer (0), out.getWritePointer (out.getNumChannels() > 1 ? 1 : 0) };
    synth->renderBlock (channels, (size_t) out.getNumSamples(), 1);
}

void SfzInstrument::allNotesOff() noexcept { synth->allSoundOff(); }

//==============================================================================
DrumInstrument::DrumInstrument (std::function<DrumKit (double)> f) : makeKit (std::move (f)) {}

void DrumInstrument::prepare (double sampleRate, int)
{
    if (sampler == nullptr || std::abs (kit.sampleRate - sampleRate) > 0.5)
    {
        sampler.reset();
        kit = makeKit (sampleRate);
        sampler = std::make_unique<DrumSampler> (kit);
    }
    sampler->setCurrentPlaybackSampleRate (sampleRate);
}

void DrumInstrument::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) noexcept
{
    out.clear();
    if (sampler != nullptr)
        sampler->renderNextBlock (out, midi, 0, out.getNumSamples());
}

void DrumInstrument::allNotesOff() noexcept
{
    if (sampler != nullptr)
        sampler->allNotesOff (0, false);
}

} // namespace jaudios::synth
