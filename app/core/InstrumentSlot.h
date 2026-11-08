#pragma once

#include "PluginHost.h"

#include "engine/synth/SfzInstrument.h"

namespace jaudios
{

/** What plays a generated channel, in a form a project file keeps: a built-in SFZ preset (or an
    SFZ file), the built-in drum kit, or a VST3 plugin with its state (synth-and-vst3-host: "state
    save/restore in project file"). */
struct InstrumentSlot
{
    enum class Kind { builtin, sfzFile, drums, plugin };
    Kind kind = Kind::builtin;
    std::string preset;              // builtin
    juce::File file;                 // sfzFile
    juce::PluginDescription plugin;  // plugin
    juce::MemoryBlock state;         // plugin: getStateInformation()

    juce::var toVar() const;
    static std::optional<InstrumentSlot> fromVar (const juce::var&, juce::String& error);
};

/** The slot describing a live instrument (plugin state captured now; message thread). */
std::optional<InstrumentSlot> captureSlot (synth::Instrument&);

/** Builds the slot's instrument (a plugin is instantiated by `host` and gets its state back). */
std::shared_ptr<synth::Instrument> instantiate (const InstrumentSlot&, PluginHost& host, double sampleRate, int blockSize, juce::String& error);

/** A project file: { "version": 1, "slots": { "chords": {...}, "bass": {...}, "drums": {...} } }. */
struct ProjectInstruments
{
    std::map<std::string, InstrumentSlot> slots; // "chords", "bass", "drums"
    bool save (const juce::File&) const;
    static std::optional<ProjectInstruments> load (const juce::File&, juce::String& error);
};

} // namespace jaudios
