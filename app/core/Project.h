#pragma once

#include "AudioEngine.h"
#include "InstrumentSlot.h"

namespace jaudios
{

/** Channel ids in files and on the command line: "source", "chords", "bass", "drums",
    "drums_stem", "bass_stem", "other_stem", "vocals_stem". */
juce::String channelName (ChannelId);
std::optional<ChannelId> channelFromName (juce::StringRef);

/** A project file (export-and-drag-drop step 4): `<song>.jaudios.json` plus the files it lists as
    assets next to it. Version 2 adds everything but the instruments to version 1 (which only had
    "slots"); both load, and ProjectInstruments::load reads the slots of either. */
struct Project
{
    struct Source
    {
        juce::String file;          // as given (absolute)
        juce::String sha256;        // of the file, to recognise it after a move
        double sampleRate = 0.0;
        juce::int64 numSamples = 0;
    } source;
    struct Analysis
    {
        juce::String key;           // "A minor"
        double bpm = 0.0;
        std::vector<double> beats, downbeats; // seconds
    } analysis;
    struct Generation
    {
        juce::String style, progression;      // "house", "vi IV I V"
        juce::StringArray chords;             // one name per bar slot, e.g. "Am"
        juce::String bassPattern, drumPattern;
    } generation;
    std::map<std::string, InstrumentSlot> instruments; // "chords", "bass", "drums"
    std::map<std::string, juce::String> fx;            // channel name -> its FX state (XML)
    juce::StringArray assets;                          // exported files, relative to the project file

    bool save (const juce::File&) const;
    static std::optional<Project> load (const juce::File&, juce::String& error);

    /** Every channel's FX state from the engine (only channels with a stage switched on). */
    void captureFx (AudioEngine&);
    /** Restores the FX states into the engine; false if a state is unreadable. */
    bool applyFx (AudioEngine&) const;
};

} // namespace jaudios
