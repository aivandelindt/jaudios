#pragma once

#include "engine/synth/Instrument.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace jaudios
{

/** VST3 hosting (synth-and-vst3-host). Plugins are scanned out of process, one file at a time, by
    jaudios-plugin-scanner (a plugin that crashes or hangs while being scanned only takes the
    scanner down and is recorded as failed); the known list persists as XML. Message thread. */
class PluginHost
{
public:
    explicit PluginHost (juce::File scannerExecutable = defaultScanner());

    /** jaudios-plugin-scanner next to the running executable (app bundle: Contents/MacOS). */
    static juce::File defaultScanner();
    /** ~/Library/Application Support/JAudios/plugins.xml (macOS) or <app data>/JAudios/plugins.xml;
        JAUDIOS_PLUGIN_LIST overrides. */
    static juce::File defaultListFile();
    /** The VST3 format's standard folders (e.g. /Library/Audio/Plug-Ins/VST3, ~/Library/...). */
    juce::FileSearchPath defaultSearchPath();

    struct ScanResult
    {
        juce::Array<juce::PluginDescription> found;
        juce::StringArray failed;   // "<file>: <reason>" (crashed, timed out, not a plugin)
    };

    /** Scans every VST3 under `paths` (recursively), one scanner process per file, each with
        `timeoutMs`. Found plugins join the known list. `progress` gets each file before it is scanned. */
    ScanResult scan (const juce::FileSearchPath& paths, int timeoutMs = 30000, const std::function<void (const juce::String&)>& progress = {});
    /** One file, out of process. */
    bool scanFile (const juce::String& file, juce::Array<juce::PluginDescription>& found, juce::String& error, int timeoutMs = 30000);

    bool saveList (const juce::File&) const;
    bool loadList (const juce::File&);
    juce::KnownPluginList& known() noexcept { return knownList; }
    juce::AudioPluginFormatManager& formats() noexcept { return formatManager; }

    /** A known plugin by (case-insensitive) name, or by file / identifier string. */
    std::optional<juce::PluginDescription> find (const juce::String& nameOrFile) const;

    /** Instantiates (in this process: only scanned, known-good plugins should get here). */
    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription&, double sampleRate, int blockSize, juce::String& error);
    /** create() wrapped as a channel instrument that remembers its description. */
    std::shared_ptr<class PluginInstrument> createInstrument (const juce::PluginDescription&, double sampleRate, int blockSize, juce::String& error);

private:
    juce::File scanner;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownList;
};

/** A hosted plugin as a channel instrument. prepare() configures the plugin (stereo out, MIDI in);
    process() hands it the block's MIDI through a pre-reserved buffer and copies its first two output
    channels (our wrapper allocates nothing; what the plugin does inside is up to the plugin). */
class PluginInstrument final : public synth::Instrument
{
public:
    /** `source` is the description the plugin was created from (what a project file must keep: a
        live VST3 instance reports its inner binary, not the .vst3 bundle). Empty = ask the instance. */
    explicit PluginInstrument (std::unique_ptr<juce::AudioPluginInstance>, juce::PluginDescription source = {});
    ~PluginInstrument() override;

    juce::String name() const override;
    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) noexcept override;
    void allNotesOff() noexcept override;

    juce::AudioPluginInstance& plugin() noexcept { return *instance; }
    const juce::PluginDescription& description() const noexcept { return source; }
    /** The plugin's own state blob (getStateInformation); message thread. */
    juce::MemoryBlock getState();
    void setState (const juce::MemoryBlock&);
    /** Sets a parameter (by name or ID, case-insensitive) to a normalised 0..1 value. */
    bool setParameter (const juce::String& nameOrId, float normalised);
    /** "name=value text" for every automatable parameter (reports and tests). */
    juce::StringPairArray parameterValues() const;

private:
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::PluginDescription source;
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midiScratch;
    std::atomic<bool> pendingAllOff { false };
    int preparedBlock = 0;
};

} // namespace jaudios
