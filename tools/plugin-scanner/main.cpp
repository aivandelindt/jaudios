// jaudios-plugin-scanner: scans ONE plugin file and writes what it found as XML, in its own
// process, so a plugin that crashes or hangs while being scanned cannot take the app down
// (synth-and-vst3-host: "sandboxed scanner process to survive crashes"). The host
// (app/core/PluginHost) runs it per file with a timeout.
//
//   jaudios-plugin-scanner <plugin file or bundle> <out.xml>
//   exit 0: found >= 1 plugin; 2: not a plugin / none found; 3: bad usage or unwritable output

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <iostream>

int main (int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: jaudios-plugin-scanner <plugin> <out.xml>\n";
        return 3;
    }
    const juce::ScopedJuceInitialiser_GUI juceInit; // VST3 factories expect a message thread
    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

    const juce::String path = juce::String::fromUTF8 (argv[1]);
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : formats.getFormats())
        if (format->fileMightContainThisPluginType (path))
            format->findAllTypesForFile (found, path);

    juce::XmlElement xml ("JAUDIOS_PLUGIN_SCAN");
    xml.setAttribute ("file", path);
    for (auto* d : found)
        xml.addChildElement (d->createXml().release());
    if (! xml.writeTo (juce::File (juce::String::fromUTF8 (argv[2]))))
        return 3;
    std::cout << found.size() << " plugin(s) in " << path << "\n";
    return found.isEmpty() ? 2 : 0;
}
