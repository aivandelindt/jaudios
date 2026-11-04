// app-cli: headless harness. Every feature must work here before it gets UI.
//
//   app-cli info
//   app-cli import  <audio-file>                decode + resample to 44.1 kHz, print capture metadata
//   app-cli render  <audio-file> [--block N]   play the imported file through the engine graph offline
//   app-cli analyze <audio-file>                key / tempo / beat grid as JSON

#include "app/core/AudioEngine.h"
#include "app/core/OfflineRender.h"
#include "app/core/RealtimeTrap.h"
#include "engine/analysis/AudioImport.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <iostream>
#include <optional>

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
                 "  app-cli import  <audio-file>\n"
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

std::optional<jaudios::analysis::CaptureResult> importOrReport (const juce::File& file, double& importMs)
{
    const jaudios::analysis::AudioImporter importer;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto result = importer.importFile (file);
    importMs = juce::Time::getMillisecondCounterHiRes() - t0;

    if (! result.ok())
        std::cerr << "error: " << result.error << "\n";

    return std::move (result.capture);
}

var describeCapture (const jaudios::analysis::CaptureResult& c, double importMs)
{
    float peak = 0.0f;
    double sumSquares = 0.0;
    for (int ch = 0; ch < c.buffer.getNumChannels(); ++ch)
    {
        peak = std::max (peak, c.buffer.getMagnitude (ch, 0, c.buffer.getNumSamples()));
        const float r = c.buffer.getRMSLevel (ch, 0, c.buffer.getNumSamples());
        sumSquares += (double) r * r;
    }
    const double rms = std::sqrt (sumSquares / std::max (1, c.buffer.getNumChannels()));

    return makeObject ({
        { "file", c.sourceFile.getFullPathName() },
        { "format", c.formatName },
        { "original_sample_rate", c.originalSampleRate },
        { "original_channels", c.originalNumChannels },
        { "original_length_samples", c.originalLengthSamples },
        { "resampled", c.originalSampleRate != c.sampleRate },
        { "sample_rate", c.sampleRate },
        { "channels", c.buffer.getNumChannels() },
        { "length_samples", c.buffer.getNumSamples() },
        { "duration_sec", c.durationSeconds() },
        { "peak_dbfs", juce::Decibels::gainToDecibels (peak) },
        { "rms_dbfs", juce::Decibels::gainToDecibels ((float) rms) },
        { "import_ms", importMs },
    });
}

int cmdImport (const juce::File& file)
{
    double importMs = 0.0;
    const auto capture = importOrReport (file, importMs);
    if (! capture)
        return 1;

    printJson (describeCapture (*capture, importMs));
    return 0;
}

int cmdRender (const juce::File& file, int blockSize)
{
    double importMs = 0.0;
    auto capture = importOrReport (file, importMs);
    if (! capture)
        return 1;

    const double sampleRate = capture->sampleRate;
    auto source = std::make_shared<const juce::AudioBuffer<float>> (std::move (capture->buffer));

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
    std::cerr << "analyze: not implemented yet (lands with key-and-tempo-analysis)\n";
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

    if (cmd == "import")
        return cmdImport (file);

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
