// app-cli: headless harness. Every feature must work here before it gets UI.
//
//   app-cli info
//   app-cli import  <audio-file>                decode + resample to 44.1 kHz, print capture metadata
//   app-cli render  <audio-file> [--block N]   play the imported file through the engine graph offline
//   app-cli analyze <audio-file>                key / tempo / beat grid as JSON
//   app-cli separate <audio-file> [--out DIR] [--guide other+vocals] [--no-cache]
//                                               4 stems (HT-Demucs) + guide.wav in DIR, JSON report;
//                                               stems are cached by audio SHA-256 (StemCache)

#include "app/core/AnalysisService.h"
#include "app/core/AudioEngine.h"
#include "app/core/OfflineRender.h"
#include "app/core/RealtimeTrap.h"
#include "engine/analysis/Analysis.h"
#include "engine/analysis/AudioImport.h"
#include "engine/analysis/key/KeyDetector.h"
#include "engine/analysis/stems/StemCache.h"
#include "engine/analysis/stems/StemSeparator.h"

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
                 "  app-cli analyze <audio-file>\n"
                 "  app-cli separate <audio-file> [--out DIR] [--guide other+vocals] [--no-cache]\n"
                 "                   (default DIR: <file name>.stems/ next to the file; cache: JAUDIOS_STEM_CACHE_DIR)\n";
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

int cmdAnalyze (const juce::File& file)
{
    double importMs = 0.0;
    const auto capture = importOrReport (file, importMs);
    if (! capture)
        return 1;

    // Same threading as the app: analysis runs on the AnalysisService worker, result comes back
    // on this (message) thread.
    auto keyDetector = jaudios::analysis::makeKeyDetector (jaudios::analysis::AppConfig::fromEnvironment());
    jaudios::AnalysisService service;
    std::optional<jaudios::analysis::AnalysisResult> result;

    service.submit<jaudios::analysis::AnalysisResult> ([&] { return jaudios::analysis::analyze (*capture, *keyDetector.detector); },
                                                       [&] (jaudios::analysis::AnalysisResult r) { result = std::move (r); });
    service.waitUntilIdle();

    auto json = jaudios::analysis::toJson (*result, *capture, keyDetector.warning);
    json.getDynamicObject()->setProperty ("import_ms", importMs);
    printJson (json);
    return 0;
}

void writeWav (const juce::File& dest, const juce::AudioBuffer<float>& b, double sampleRate, bool& ok)
{
    dest.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (dest);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels (b.getNumChannels())
                                                   .withBitsPerSample (32)); // float: stems may exceed 0 dBFS
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()))
    {
        std::cerr << "error: cannot write " << dest.getFullPathName() << "\n";
        ok = false;
    }
}

int cmdSeparate (const juce::File& file, juce::File outDir, const jaudios::analysis::stems::GuideSelection& guide, bool useCache)
{
    using namespace jaudios::analysis;
    double importMs = 0.0;
    const auto capture = importOrReport (file, importMs);
    if (! capture)
        return 1;
    if (outDir == juce::File())
        outDir = file.getSiblingFile (file.getFileNameWithoutExtension() + ".stems");

    const auto config = AppConfig::fromEnvironment();
    const auto model = findModelFile (config, "htdemucs.onnx");
    if (model == juce::File())
    {
        std::cerr << "error: htdemucs.onnx not found (python3 tools/models.py --only demucs --python <env>, or set JAUDIOS_MODELS_DIR)\n";
        return 1;
    }
    const auto cacheDirEnv = juce::SystemStats::getEnvironmentVariable ("JAUDIOS_STEM_CACHE_DIR", {});
    const stems::StemCache cache (cacheDirEnv.isNotEmpty() ? juce::File (cacheDirEnv) : stems::StemCache::defaultDirectory());

    // Same threading as the app: model load, cache and separation on the AnalysisService worker.
    jaudios::AnalysisService service;
    std::optional<stems::StemSeparator::Result> result;
    juce::String provider, origin, loadError, key;
    double loadMs = 0.0;
    bool cacheHit = false;
    service.submit<stems::StemSeparator::Result> (
        [&] {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            stems::StemSeparator sep ({ model, config.onnxCoreML });
            loadMs = juce::Time::getMillisecondCounterHiRes() - t0;
            provider = sep.executionProvider();
            origin = sep.modelOrigin();
            loadError = sep.loadError();
            if (! sep.isReady())
                return stems::StemSeparator::Result { {}, loadError };

            key = stems::StemCache::keyFor (capture->buffer, capture->sampleRate, sep.cacheId());
            if (useCache)
                if (auto cached = cache.load (key))
                {
                    cacheHit = true;
                    stems::StemSeparator::Result r;
                    r.stems = std::move (cached);
                    return r;
                }

            auto r = sep.separate (capture->buffer, capture->sampleRate, [] (double p) {
                std::cerr << "\rseparating " << juce::roundToInt (p * 100.0) << " %" << std::flush;
            });
            std::cerr << "\n";
            if (useCache && r.stems && cache.store (key, *r.stems))
                cache.prune();
            return r;
        },
        [&] (stems::StemSeparator::Result r) { result = std::move (r); });
    service.waitUntilIdle();

    if (! result || ! result->stems)
    {
        std::cerr << "error: " << (result && result->error.isNotEmpty() ? result->error : loadError) << "\n";
        return 1;
    }

    outDir.createDirectory();
    const auto& st = *result->stems;
    const int n = st.getNumSamples();
    auto rmsDb = [] (double sq, int count) { return 20.0 * std::log10 (std::max (std::sqrt (sq / std::max (1, count)), 1e-12)); };

    juce::Array<var> files;
    bool ok = true;
    for (auto s : stems::allStems)
    {
        const auto& b = st[s];
        const auto dest = outDir.getChildFile (juce::String (stems::stemName (s)) + ".wav");
        writeWav (dest, b, st.sampleRate, ok);
        double sq = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < n; ++i)
                sq += (double) b.getSample (ch, i) * b.getSample (ch, i);
        files.add (makeObject ({ { "stem", stems::stemName (s) }, { "path", dest.getFullPathName() },
                                 { "rms_dbfs", rmsDb (sq, n * b.getNumChannels()) } }));
    }

    const auto guideFile = outDir.getChildFile ("guide.wav");
    if (! guide.empty())
        writeWav (guideFile, stems::mixGuide (st, guide), st.sampleRate, ok);
    if (! ok)
        return 1;

    // skill acceptance: sum of stems vs the original, RMS
    double residual = 0.0;
    const auto& mix = capture->buffer;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
        {
            double sum = 0.0;
            for (const auto& b : st.buffers)
                sum += b.getSample (ch, i);
            const double d = sum - mix.getSample (std::min (ch, mix.getNumChannels() - 1), i);
            residual += d * d;
        }

    const double durationSec = n / st.sampleRate;
    printJson (makeObject ({
        { "file", file.getFullPathName() },
        { "duration_sec", durationSec },
        { "sample_rate", st.sampleRate },
        { "model", model.getFullPathName() },
        { "model_origin", origin },
        { "execution_provider", provider },
        { "model_load_ms", loadMs },
        { "cache", useCache ? (cacheHit ? "hit" : "stored") : "off" },
        { "cache_key", key },
        { "segments", result->segments },
        { "separate_sec", result->seconds },
        { "realtime_factor", durationSec / std::max (1e-6, result->seconds) },
        { "null_residual_dbfs", rmsDb (residual, 2 * n) },
        { "stems", files },
        { "guide", guide.empty() ? var() : makeObject ({ { "stems", guide.toString() }, { "path", guideFile.getFullPathName() } }) },
        { "import_ms", importMs },
    }));
    return 0;
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

    if (cmd == "separate")
    {
        juce::File out;
        jaudios::analysis::stems::GuideSelection guide; // default other + vocals
        bool useCache = true;
        for (int i = 3; i < argc; ++i)
        {
            const juce::String a (argv[i]);
            if (a == "--no-cache")
                useCache = false;
            else if (a == "--out" && i + 1 < argc)
                out = argFile (argv[++i]);
            else if (a == "--guide" && i + 1 < argc)
            {
                const auto g = jaudios::analysis::stems::GuideSelection::fromString (argv[++i]);
                if (! g)
                {
                    std::cerr << "error: --guide takes stem names joined by '+' (drums, bass, other, vocals) or 'all'\n";
                    return 2;
                }
                guide = *g;
            }
            else
                return usage();
        }
        return cmdSeparate (file, out, guide, useCache);
    }

    return usage();
}
