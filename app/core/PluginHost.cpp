#include "PluginHost.h"

namespace jaudios
{

juce::File PluginHost::defaultScanner()
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
   #if JUCE_WINDOWS
    const juce::String name = "jaudios-plugin-scanner.exe";
   #else
    const juce::String name = "jaudios-plugin-scanner";
   #endif
    // a release bundle / install: next to the executable (Contents/MacOS)
    if (const auto beside = exe.getParentDirectory().getChildFile (name); beside.existsAsFile())
        return beside;
    // a dev build: <build>/tools/plugin-scanner/jaudios-plugin-scanner_artefacts/<Config>/, found by
    // walking up from wherever this executable is (the app, app-cli, the tests)
    const auto config = exe.getParentDirectory().getFileName(); // "Release", "Debug" (or a plain dir)
    auto dir = exe.getParentDirectory();
    for (int up = 0; up < 8 && dir != dir.getParentDirectory(); ++up, dir = dir.getParentDirectory())
        for (const auto& c : { config, juce::String ("Release"), juce::String ("Debug"), juce::String ("RelWithDebInfo") })
            if (const auto f = dir.getChildFile ("tools/plugin-scanner/jaudios-plugin-scanner_artefacts").getChildFile (c).getChildFile (name);
                f.existsAsFile())
                return f;
    return exe.getParentDirectory().getChildFile (name);
}

juce::File PluginHost::defaultListFile()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("JAUDIOS_PLUGIN_LIST", {});
    if (env.isNotEmpty())
        return juce::File (env);
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Application Support/JAudios/plugins.xml");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("JAudios/plugins.xml");
   #endif
}

PluginHost::PluginHost (juce::File scannerExecutable) : scanner (std::move (scannerExecutable))
{
    juce::addDefaultFormatsToManager (formatManager);
}

juce::FileSearchPath PluginHost::defaultSearchPath()
{
    juce::FileSearchPath paths;
    for (auto* f : formatManager.getFormats())
        paths.addPath (f->getDefaultLocationsToSearch());
    return paths;
}

bool PluginHost::scanFile (const juce::String& file, juce::Array<juce::PluginDescription>& found, juce::String& error, int timeoutMs)
{
    if (! scanner.existsAsFile())
    {
        error = "scanner not found: " + scanner.getFullPathName();
        return false;
    }
    const auto out = juce::File::createTempFile (".xml");
    juce::ChildProcess proc;
    if (! proc.start (juce::StringArray { scanner.getFullPathName(), file, out.getFullPathName() }, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        error = "could not start the scanner";
        return false;
    }
    if (! proc.waitForProcessToFinish (timeoutMs))
    {
        proc.kill();
        out.deleteFile();
        error = "timed out after " + juce::String (timeoutMs / 1000) + " s";
        return false;
    }
    const auto code = proc.getExitCode();
    std::unique_ptr<juce::XmlElement> xml (code == 0 ? juce::XmlDocument::parse (out) : nullptr);
    out.deleteFile();
    if (code != 0)
    {
        error = code == 2 ? juce::String ("no plugin in it") : "the scanner crashed or failed (exit " + juce::String ((int) code) + ")";
        return false;
    }
    if (xml == nullptr)
    {
        error = "unreadable scan result";
        return false;
    }
    for (auto* e : xml->getChildIterator())
    {
        juce::PluginDescription d;
        if (d.loadFromXml (*e))
        {
            found.add (d);
            knownList.addType (d);
        }
    }
    return ! found.isEmpty();
}

PluginHost::ScanResult PluginHost::scan (const juce::FileSearchPath& paths, int timeoutMs, const std::function<void (const juce::String&)>& progress)
{
    ScanResult r;
    juce::StringArray files;
    for (auto* f : formatManager.getFormats())
        files.addArray (f->searchPathsForPlugins (paths, true, false));
    files.removeDuplicates (false);
    for (const auto& file : files)
    {
        if (progress)
            progress (file);
        juce::String error;
        juce::Array<juce::PluginDescription> found;
        if (scanFile (file, found, error, timeoutMs))
            r.found.addArray (found);
        else
            r.failed.add (file + ": " + error);
    }
    return r;
}

bool PluginHost::saveList (const juce::File& f) const
{
    auto xml = knownList.createXml();
    f.getParentDirectory().createDirectory();
    return xml != nullptr && xml->writeTo (f);
}

bool PluginHost::loadList (const juce::File& f)
{
    if (auto xml = juce::XmlDocument::parse (f))
    {
        knownList.recreateFromXml (*xml);
        return true;
    }
    return false;
}

std::optional<juce::PluginDescription> PluginHost::find (const juce::String& key) const
{
    for (const auto& d : knownList.getTypes())
        if (d.name.equalsIgnoreCase (key) || d.descriptiveName.equalsIgnoreCase (key) || d.fileOrIdentifier == key
            || juce::File (d.fileOrIdentifier).getFileNameWithoutExtension().equalsIgnoreCase (key))
            return d;
    return std::nullopt;
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::create (const juce::PluginDescription& d, double sr, int block, juce::String& error)
{
    return formatManager.createPluginInstance (d, sr, block, error);
}

//==============================================================================
std::shared_ptr<PluginInstrument> PluginHost::createInstrument (const juce::PluginDescription& d, double sr, int block, juce::String& error)
{
    auto instance = create (d, sr, block, error);
    return instance != nullptr ? std::make_shared<PluginInstrument> (std::move (instance), d) : nullptr;
}

PluginInstrument::PluginInstrument (std::unique_ptr<juce::AudioPluginInstance> p, juce::PluginDescription d)
    : instance (std::move (p)), source (std::move (d))
{
    if (source.fileOrIdentifier.isEmpty() && instance != nullptr)
        source = instance->getPluginDescription();
}

PluginInstrument::~PluginInstrument()
{
    if (instance != nullptr)
        instance->releaseResources();
}

juce::String PluginInstrument::name() const { return instance->getName(); }

void PluginInstrument::prepare (double sampleRate, int maxBlockSize)
{
    instance->releaseResources();
    // stereo out, no audio in (an instrument); fall back to whatever the plugin defaults to
    auto layout = instance->getBusesLayout();
    if (instance->getBusCount (false) > 0)
        layout.getChannelSet (false, 0) = juce::AudioChannelSet::stereo();
    for (int i = 0; i < instance->getBusCount (true); ++i)
        layout.getChannelSet (true, i) = juce::AudioChannelSet::disabled();
    instance->setBusesLayout (layout);
    instance->setRateAndBufferSizeDetails (sampleRate, maxBlockSize);
    instance->prepareToPlay (sampleRate, maxBlockSize);
    const int channels = std::max ({ 2, instance->getTotalNumInputChannels(), instance->getTotalNumOutputChannels() });
    scratch.setSize (channels, maxBlockSize, false, true, false);
    midiScratch.ensureSize (8192);
    preparedBlock = maxBlockSize;

    // JUCE's VST3 host grows its per-instance event list the first time a block carries more events
    // than ever before (and keeps that capacity). Grow it here, off the audio thread, with one block
    // of note-offs for notes that are not playing: more events than a generated block holds.
    juce::MidiBuffer warmUp;
    for (int i = 0; i < 256; ++i)
        warmUp.addEvent (juce::MidiMessage::noteOff (1 + i / 128, i % 128), 0);
    instance->processBlock (scratch, warmUp);
    scratch.clear();
}

void PluginInstrument::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) noexcept
{
    const int n = std::min (out.getNumSamples(), preparedBlock);
    midiScratch.clear();
    if (pendingAllOff.exchange (false))
        for (int ch = 1; ch <= 16; ++ch)
            midiScratch.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
    midiScratch.addEvents (midi, 0, n, 0); // fits the space reserved in prepare
    juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), scratch.getNumChannels(), n); // no allocation: refers to scratch
    view.clear();
    instance->processBlock (view, midiScratch);
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        out.copyFrom (ch, 0, view, std::min (ch, view.getNumChannels() - 1), 0, n);
    if (out.getNumSamples() > n)
        out.clear (n, out.getNumSamples() - n);
}

void PluginInstrument::allNotesOff() noexcept { pendingAllOff.store (true); }

juce::MemoryBlock PluginInstrument::getState()
{
    juce::MemoryBlock mb;
    instance->getStateInformation (mb);
    return mb;
}

void PluginInstrument::setState (const juce::MemoryBlock& mb)
{
    instance->setStateInformation (mb.getData(), (int) mb.getSize());
}

bool PluginInstrument::setParameter (const juce::String& key, float normalised)
{
    for (auto* p : instance->getParameters())
    {
        const auto* withId = dynamic_cast<juce::HostedAudioProcessorParameter*> (p);
        if (p->getName (64).equalsIgnoreCase (key) || (withId != nullptr && withId->getParameterID().equalsIgnoreCase (key)))
        {
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
            return true;
        }
    }
    return false;
}

juce::StringPairArray PluginInstrument::parameterValues() const
{
    juce::StringPairArray values;
    for (auto* p : instance->getParameters())
        if (p->isAutomatable()) // skips hidden helpers, e.g. JUCE plugins' 2048 "MIDI CC c|n" proxies
            values.set (p->getName (64), p->getCurrentValueAsText());
    return values;
}

} // namespace jaudios
