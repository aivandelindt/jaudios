#include "SelfTest.h"

#include "PluginHost.h"
#include "SongPipeline.h"

namespace jaudios
{

juce::var runSelfTest (const juce::File& audioFile, bool& ok)
{
    using namespace analysis;
    auto* root = new juce::DynamicObject();
    const auto config = AppConfig::fromEnvironment();
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    root->setProperty ("executable", exe.getFullPathName());
    root->setProperty ("version", JAUDIOS_VERSION_STRING);

    auto* models = new juce::DynamicObject();
    for (const char* name : { "openkeyscan3.onnx", "basic_pitch.onnx", "htdemucs.onnx" })
    {
        const auto f = findModelFile (config, name);
        models->setProperty (name, f.existsAsFile() ? juce::var (f.getFullPathName()) : juce::var());
    }
    root->setProperty ("models", juce::var (models));

    auto key = makeKeyDetector (config);
    root->setProperty ("key_backend", toString (key.backend));
    if (key.warning.isNotEmpty())
        root->setProperty ("key_warning", key.warning);
    const auto scanner = PluginHost::defaultScanner();
    root->setProperty ("plugin_scanner", scanner.existsAsFile() ? juce::var (scanner.getFullPathName()) : juce::var());

    ok = key.backend == KeyBackend::onnx && findModelFile (config, "basic_pitch.onnx").existsAsFile() && scanner.existsAsFile();
    if (audioFile != juce::File())
    {
        auto* song = new juce::DynamicObject();
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto imported = AudioImporter().importFile (audioFile);
        if (! imported.ok())
        {
            song->setProperty ("error", imported.error);
            ok = false;
        }
        else
        {
            juce::String error;
            const auto analysed = analyseSong (*imported.capture, SongSettings {}, error, key.detector.get());
            if (! analysed)
            {
                song->setProperty ("error", error);
                ok = false;
            }
            else
            {
                const auto& k = analysed->source.key;
                song->setProperty ("file", audioFile.getFullPathName());
                song->setProperty ("key", juce::String (theory::traditionalName (k)));
                song->setProperty ("camelot", juce::String (theory::camelot (k)));
                song->setProperty ("bpm", analysed->analysis.rhythm.bpm);
                song->setProperty ("progression", juce::String (analysed->progressions.front().summary (k)));
            }
        }
        song->setProperty ("seconds", (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
        root->setProperty ("song", juce::var (song));
    }
    root->setProperty ("ok", ok);
    return juce::var (root);
}

} // namespace jaudios
