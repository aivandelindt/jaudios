// app-cli: headless harness. Every feature must work here before it gets UI.
//
//   app-cli info
//   app-cli render  <audio-file> [--block N]   play the file through the engine graph offline
//   app-cli analyze <audio-file>                key / tempo / beat grid as JSON

#include "app/core/AudioEngine.h"
#include "app/core/OfflineRender.h"
#include "app/core/RealtimeTrap.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <iostream>

namespace
{

using juce::var;

var makeObject (std::initializer_list<std::pair<const char*, var>> props)
{
    auto* o = new juce::DynamicObject();
    for (const auto& [k, v] : props)
        o->setProperty (k, v);
    return var (o);
}

void printJson (const var& v)
{
    std::cout << juce::JSON::toString (v, false, 4).toStdString() << std::endl;
}

int usage()
{
    std::cerr << "usage:\n"
                 "  app-cli info\n"
                 "  app-cli render  <audio-file> [--block N]\n"
                 "  app-cli analyze <audio-file>\n";
    return 2;
}

juce::File argFile (const juce::String& path)
{
    return juce::File::getCurrentWorkingDirectory().getChildFile (path);
}

int cmdInfo()
{
    jaudios::AudioEngine engine;
    engine.prepareOffline (44100.0, jaudios::AudioEngine::defaultBlockSize);

    printJson (makeObject ({
        { "app", "JAudios" },
        { "version", JAUDIOS_VERSION_STRING },
        { "juce", juce::SystemStats::getJUCEVersion() },
        { "block_size", engine.getBlockSize() },
        { "offline_latency_ms", engine.getOutputLatencyMs() },
        { "rt_trap_installed", jaudios::rt::isTrapInstalled() },
    }));
    return 0;
}

/** Scaffold-stage reader; replaced by engine/analysis AudioImport in the audio-capture step. */
std::shared_ptr<juce::AudioBuffer<float>> readRaw (const juce::File& file, double& sampleRate)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return nullptr;

    auto buffer = std::make_shared<juce::AudioBuffer<float>> ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (buffer.get(), 0, (int) reader->lengthInSamples, 0, true, true);
    sampleRate = reader->sampleRate;
    return buffer;
}

int cmdRender (const juce::File& file, int blockSize)
{
    double sampleRate = 0.0;
    auto source = readRaw (file, sampleRate);
    if (source == nullptr)
    {
        std::cerr << "error: cannot read " << file.getFullPathName() << "\n";
        return 1;
    }

    jaudios::AudioEngine engine;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto report = jaudios::renderThroughEngine (engine, source, sampleRate, blockSize);
    const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - t0;

    const var rtStats = jaudios::rt::isTrapInstalled()
        ? makeObject ({ { "allocations", (juce::int64) report.rtStats.allocations },
                        { "deallocations", (juce::int64) report.rtStats.deallocations },
                        { "mutex_locks", (juce::int64) report.rtStats.mutexLocks } })
        : var ("not instrumented (run tools/rt_check.sh)");

    printJson (makeObject ({
        { "file", file.getFullPathName() },
        { "sample_rate", sampleRate },
        { "channels", source->getNumChannels() },
        { "duration_sec", source->getNumSamples() / sampleRate },
        { "block_size", report.blockSize },
        { "blocks", report.numBlocks },
        { "output_latency_ms", report.latencyMs },
        { "null_test_max_abs_diff", report.maxAbsDiffVsSource },
        { "render_ms", elapsedMs },
        { "realtime_factor", (source->getNumSamples() / sampleRate * 1000.0) / juce::jmax (1.0e-6, elapsedMs) },
        { "rt", rtStats },
    }));
    return 0;
}

int cmdAnalyze (const juce::File&)
{
    std::cerr << "analyze: not implemented yet (lands with audio-capture + key-and-tempo-analysis)\n";
    return 3;
}

} // namespace

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juceInit; // message manager for graph/AsyncUpdater

    if (argc < 2)
        return usage();

    const juce::String cmd (argv[1]);

    if (cmd == "info")
        return cmdInfo();

    if (argc < 3)
        return usage();

    const auto file = argFile (argv[2]);
    if (! file.existsAsFile())
    {
        std::cerr << "error: no such file: " << file.getFullPathName() << "\n";
        return 1;
    }

    if (cmd == "render")
    {
        int block = jaudios::AudioEngine::defaultBlockSize;
        for (int i = 3; i + 1 < argc; ++i)
            if (juce::String (argv[i]) == "--block")
                block = juce::jlimit (16, 8192, juce::String (argv[i + 1]).getIntValue());
        return cmdRender (file, block);
    }

    if (cmd == "analyze")
        return cmdAnalyze (file);

    return usage();
}
