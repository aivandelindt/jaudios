// app-cli: headless harness. Every feature must work here before it gets UI.
//
//   app-cli info
//   app-cli import  <audio-file>                decode + resample to 44.1 kHz, print capture metadata
//   app-cli render  <audio-file> [--block N]   play the imported file through the engine graph offline
//   app-cli analyze <audio-file>                key / tempo / beat grid as JSON
//   app-cli separate <audio-file> [--out DIR] [--guide other+vocals] [--no-cache]
//                                               4 stems (HT-Demucs) + guide.wav in DIR, JSON report;
//                                               stems are cached by audio SHA-256 (StemCache)
//   app-cli transcribe <audio-file> [--stem mix|drums|bass|other|vocals|guide] [--mono|--poly]
//                      [--quantize 4|8|16|32|off] [--onset T] [--frame T] [--min-ms MS] [--out FILE.mid] [--no-cache]
//                                               Basic Pitch notes as JSON + a MIDI file (bass stem: monophonic,
//                                               default 1/16 quantise on the detected beat grid, swing kept)
//   app-cli chords <audio-file> [--style pop|house|techno|trance|lofi|rnb|cinematic] [--rhythm 1|2|half]
//                  [--sevenths] [--stems] [--top N] [--seed N] [--voicing close|open|drop2] [--humanize N]
//                  [--out FILE.mid] [--no-cache]
//                                               chord progressions that fit key, melody, bass and chroma
//                                               (top N as JSON, top-1 as MIDI)
//   app-cli bass <audio-file> [--style ...] [--pattern NAME] [--stems] [--grid 8|16|32] [--swing 0..0.75]
//                [--out FILE.mid] [--no-cache] | app-cli bass --list-patterns
//                                               a bassline on the top chord progression and the detected
//                                               groove (kick sidechain hints), as JSON + MIDI
//   app-cli drums <audio-file> [--style ...] [--pattern NAME] [--intensity 1..5] [--no-kick]
//                 [--transition fill|riser_hit|drop|half_time|none] [--phrase BARS] [--swing 0..0.75]
//                 [--kit DIR] [--split] [--out DIR] | app-cli drums --list-patterns
//                                               a groove on the detected downbeats, rendered with the
//                                               built-in (or a user) kit: drums.wav + drums.mid, or one
//                                               WAV + MIDI per element (--split)

#include "app/core/AnalysisService.h"
#include "app/core/AudioEngine.h"
#include "app/core/HarmonySource.h"
#include "app/core/OfflineRender.h"
#include "app/core/RealtimeTrap.h"
#include "engine/analysis/Analysis.h"
#include "engine/analysis/AudioImport.h"
#include "engine/analysis/key/KeyDetector.h"
#include "engine/analysis/stems/StemCache.h"
#include "engine/analysis/stems/StemSeparator.h"
#include "engine/analysis/tempo/BeatTracker.h"
#include "engine/analysis/tempo/KickDetector.h"
#include "engine/generators/BassGenerator.h"
#include "engine/generators/DrumGenerator.h"
#include "engine/synth/DrumSampler.h"
#include "engine/analysis/transcription/AudioToMidi.h"
#include "engine/analysis/transcription/Quantize.h"

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
                 "                   (default DIR: <file name>.stems/ next to the file; cache: JAUDIOS_STEM_CACHE_DIR)\n"
                 "  app-cli transcribe <audio-file> [--stem mix|drums|bass|other|vocals|guide] [--mono|--poly]\n"
                 "                   [--quantize 4|8|16|32|off] [--onset T] [--frame T] [--min-ms MS] [--out FILE.mid] [--no-cache]\n"
                 "  app-cli chords <audio-file> [--style pop|house|techno|trance|lofi|rnb|cinematic] [--rhythm 1|2|half]\n"
                 "                   [--sevenths] [--stems] [--top N] [--seed N] [--voicing close|open|drop2] [--humanize N]\n"
                 "                   [--out FILE.mid] [--no-cache]\n"
                 "  app-cli bass <audio-file> [--style ...] [--pattern NAME] [--stems] [--grid 8|16|32] [--swing 0..0.75]\n"
                 "                   [--out FILE.mid] [--no-cache]\n"
                 "  app-cli bass --list-patterns\n"
                 "  app-cli drums <audio-file> [--style ...] [--pattern NAME] [--intensity 1..5] [--no-kick]\n"
                 "                   [--transition fill|riser_hit|drop|half_time|none] [--phrase BARS] [--swing 0..0.75]\n"
                 "                   [--kit DIR] [--split] [--out DIR]\n"
                 "  app-cli drums --list-patterns\n";
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

/** Stems for a capture, from the stem cache or HT-Demucs; on the AnalysisService worker like the app. */
struct SeparationRun
{
    std::optional<jaudios::analysis::stems::StemSeparator::Result> result;
    juce::String provider, origin, error, key;
    double loadMs = 0.0;
    bool cacheHit = false;
    bool ok() const { return result && result->stems; }
};

SeparationRun runSeparation (const jaudios::analysis::CaptureResult& capture, bool useCache)
{
    using namespace jaudios::analysis;
    SeparationRun run;
    const auto config = AppConfig::fromEnvironment();
    const auto model = findModelFile (config, "htdemucs.onnx");
    if (model == juce::File())
    {
        run.error = "htdemucs.onnx not found (python3 tools/models.py --only demucs --python <env>, or set JAUDIOS_MODELS_DIR)";
        return run;
    }
    const auto cacheDirEnv = juce::SystemStats::getEnvironmentVariable ("JAUDIOS_STEM_CACHE_DIR", {});
    const stems::StemCache cache (cacheDirEnv.isNotEmpty() ? juce::File (cacheDirEnv) : stems::StemCache::defaultDirectory());

    jaudios::AnalysisService service;
    service.submit<stems::StemSeparator::Result> (
        [&] {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            stems::StemSeparator sep ({ model, config.onnxCoreML });
            run.loadMs = juce::Time::getMillisecondCounterHiRes() - t0;
            run.provider = sep.executionProvider();
            run.origin = sep.modelOrigin();
            if (! sep.isReady())
                return stems::StemSeparator::Result { {}, sep.loadError() };

            run.key = stems::StemCache::keyFor (capture.buffer, capture.sampleRate, sep.cacheId());
            if (useCache)
                if (auto cached = cache.load (run.key))
                {
                    run.cacheHit = true;
                    stems::StemSeparator::Result r;
                    r.stems = std::move (cached);
                    return r;
                }

            auto r = sep.separate (capture.buffer, capture.sampleRate, [] (double p) {
                std::cerr << "\rseparating " << juce::roundToInt (p * 100.0) << " %" << std::flush;
            });
            std::cerr << "\n";
            if (useCache && r.stems && cache.store (run.key, *r.stems))
                cache.prune();
            return r;
        },
        [&] (stems::StemSeparator::Result r) { run.result = std::move (r); });
    service.waitUntilIdle();
    if (! run.ok() && run.error.isEmpty())
        run.error = run.result ? run.result->error : juce::String ("separation did not run");
    return run;
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

    const auto run = runSeparation (*capture, useCache);
    if (! run.ok())
    {
        std::cerr << "error: " << run.error << "\n";
        return 1;
    }
    const auto& result = run.result;
    const auto& provider = run.provider;
    const auto& origin = run.origin;
    const auto& key = run.key;
    const auto loadMs = run.loadMs;
    const auto cacheHit = run.cacheHit;
    const auto model = findModelFile (AppConfig::fromEnvironment(), "htdemucs.onnx");

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

struct TranscribeOptions
{
    juce::String stem = "mix";
    std::optional<bool> monophonic; // default: on for the bass stem
    int quantize = 16;              // 0 = off
    jaudios::analysis::transcription::NoteParams notes;
    juce::File out;
    bool useCache = true;
};

/** Standard MIDI file (format 1, 480 PPQ) at a constant tempo; seconds -> ticks at that tempo. */
bool writeMidiFile (const juce::File& dest, const juce::MidiMessageSequence& seq, double bpm)
{
    constexpr int ppq = 480;
    const double ticksPerSecond = ppq * bpm / 60.0;
    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60'000'000.0 / bpm)), 0.0);
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    for (const auto* e : seq)
        track.addEvent (e->message, std::round (e->message.getTimeStamp() * ticksPerSecond) - e->message.getTimeStamp());
    track.updateMatchedPairs();
    juce::MidiFile file;
    file.setTicksPerQuarterNote (ppq);
    file.addTrack (track);
    dest.getParentDirectory().createDirectory();
    dest.deleteFile();
    juce::FileOutputStream out (dest);
    return out.openedOk() && file.writeTo (out, 1);
}

int cmdTranscribe (const juce::File& file, TranscribeOptions opt)
{
    using namespace jaudios::analysis;
    namespace tr = jaudios::analysis::transcription;
    double importMs = 0.0;
    const auto capture = importOrReport (file, importMs);
    if (! capture)
        return 1;

    // what to transcribe: the mix, one stem, or the guide stems
    juce::AudioBuffer<float> audio = capture->buffer;
    juce::String separation = "none";
    if (opt.stem != "mix")
    {
        const auto run = runSeparation (*capture, opt.useCache);
        if (! run.ok())
        {
            std::cerr << "error: " << run.error << "\n";
            return 1;
        }
        separation = run.cacheHit ? "cache" : "htdemucs (" + run.provider + ")";
        const auto& st = *run.result->stems;
        if (opt.stem == "guide")
            audio = stems::mixGuide (st);
        else if (const auto s = stems::stemFromName (opt.stem))
            audio = st[*s];
        else
        {
            std::cerr << "error: --stem takes mix, drums, bass, other, vocals or guide\n";
            return 2;
        }
    }

    const auto config = AppConfig::fromEnvironment();
    const auto model = findModelFile (config, "basic_pitch.onnx");
    if (model == juce::File())
    {
        std::cerr << "error: basic_pitch.onnx not found (python3 tools/models.py --only basic_pitch, or set JAUDIOS_MODELS_DIR)\n";
        return 1;
    }

    tr::TranscriptionSettings settings;
    settings.notes = opt.notes;
    settings.monophonic = opt.monophonic.value_or (opt.stem == "bass");

    // Same threading as the app: beat grid + transcription on the AnalysisService worker.
    jaudios::AnalysisService service;
    std::optional<tr::Transcription> result;
    RhythmResult rhythm;
    tr::QuantizeResult quantized;
    service.submit<tr::Transcription> (
        [&] {
            juce::AudioBuffer<float> mono (1, capture->buffer.getNumSamples());
            mono.clear();
            for (int ch = 0; ch < capture->buffer.getNumChannels(); ++ch)
                mono.addFrom (0, 0, capture->buffer, ch, 0, mono.getNumSamples(), 1.0f / (float) capture->buffer.getNumChannels());
            rhythm = analyzeRhythm (mono.getReadPointer (0), mono.getNumSamples(), capture->sampleRate); // grid of the mix
            tr::AudioToMidi a2m (model);
            auto t = a2m.transcribe (audio, capture->sampleRate, settings);
            if (t.ok() && opt.quantize > 0)
            {
                tr::QuantizeSettings q;
                q.division = opt.quantize;
                quantized = tr::quantize (t.notes, rhythm.beats, q);
                t.notes = quantized.notes;
                t.midi = tr::toMidiSequence (t.notes, settings.multiplePitchBends);
            }
            return t;
        },
        [&] (tr::Transcription t) { result = std::move (t); });
    service.waitUntilIdle();
    if (! result || ! result->ok())
    {
        std::cerr << "error: " << (result ? result->error : juce::String ("transcription did not run")) << "\n";
        return 1;
    }

    if (opt.out == juce::File())
        opt.out = file.getSiblingFile (file.getFileNameWithoutExtension() + "." + opt.stem + ".mid");
    const double bpm = rhythm.bpm > 0.0 ? rhythm.bpm : 120.0;
    if (! writeMidiFile (opt.out, result->midi, bpm))
    {
        std::cerr << "error: cannot write " << opt.out.getFullPathName() << "\n";
        return 1;
    }

    juce::Array<var> notes;
    double confidence = 0.0;
    int lo = 127, hi = 0;
    for (const auto& n : result->notes)
    {
        notes.add (makeObject ({ { "start", n.startSec }, { "end", n.endSec }, { "pitch", n.pitch },
                                 { "velocity", n.velocity }, { "confidence", n.confidence } }));
        confidence += n.confidence;
        lo = std::min (lo, n.pitch);
        hi = std::max (hi, n.pitch);
    }
    printJson (makeObject ({
        { "file", file.getFullPathName() },
        { "stem", opt.stem },
        { "separation", separation },
        { "monophonic", settings.monophonic },
        { "params", makeObject ({ { "onset_threshold", settings.notes.onsetThreshold }, { "frame_threshold", settings.notes.frameThreshold },
                                  { "min_note_ms", settings.notes.minNoteMs } }) },
        { "bpm", bpm },
        { "quantize", opt.quantize > 0 ? var (makeObject ({ { "division", opt.quantize }, { "swing", quantized.swing },
                                                            { "beats", (int) rhythm.beats.size() } })) : var ("off") },
        { "note_count", (int) result->notes.size() },
        { "pitch_range", result->notes.empty() ? var() : var (juce::MidiMessage::getMidiNoteName (lo, true, true, 4) + ".."
                                                               + juce::MidiMessage::getMidiNoteName (hi, true, true, 4)) },
        { "mean_confidence", result->notes.empty() ? 0.0 : confidence / (double) result->notes.size() },
        { "transcribe_sec", result->seconds },
        { "midi_file", opt.out.getFullPathName() },
        { "notes", notes },
        { "import_ms", importMs },
    }));
    return 0;
}

struct ChordOptions
{
    jaudios::generators::EngineSettings engine;
    jaudios::generators::RenderSettings render;
    bool stems = false, useCache = true;
    juce::File out;
};

/** Key + beat grid + melody / bass evidence + chord suggestions (shared by `chords` and `bass`). */
struct HarmonyRun
{
    jaudios::analysis::AnalysisResult analysis;
    jaudios::generators::HarmonicSource source;
    std::vector<jaudios::generators::Progression> progressions;
    std::vector<std::vector<jaudios::generators::Alternative>> alts;
    std::optional<jaudios::analysis::stems::Stems> stems;
    std::vector<jaudios::analysis::transcription::Note> vocals; // the vocal stem's notes (--stems)
    juce::String separation = "none", error;
    double engineMs = 0.0;
    int melodyNotes = 0, bassNotes = 0;
};

std::optional<HarmonyRun> runHarmony (const jaudios::analysis::CaptureResult& capture, const jaudios::generators::EngineSettings& engine,
                                      bool useStems, bool useCache)
{
    using namespace jaudios::analysis;
    namespace gen = jaudios::generators;
    namespace tr = jaudios::analysis::transcription;
    const auto config = AppConfig::fromEnvironment();
    const auto bpModel = findModelFile (config, "basic_pitch.onnx");
    if (bpModel == juce::File())
    {
        std::cerr << "error: basic_pitch.onnx not found (python3 tools/models.py --only basic_pitch, or set JAUDIOS_MODELS_DIR)\n";
        return std::nullopt;
    }

    HarmonyRun o;
    // melody / bass evidence: the guide + bass stems (--stems), or the mix split at C3
    if (useStems)
    {
        const auto run = runSeparation (capture, useCache);
        if (! run.ok())
        {
            std::cerr << "error: " << run.error << "\n";
            return std::nullopt;
        }
        o.separation = run.cacheHit ? "cache" : "htdemucs (" + run.provider + ")";
        o.stems = *run.result->stems;
    }

    auto keyDetector = makeKeyDetector (config);
    jaudios::AnalysisService service;
    std::optional<HarmonyRun> result;
    service.submit<HarmonyRun> (
        [&] {
            o.analysis = analyze (capture, *keyDetector.detector);
            tr::AudioToMidi a2m (bpModel);
            std::vector<tr::Note> melody, bass;
            if (o.stems)
            {
                tr::TranscriptionSettings mono;
                mono.monophonic = true;
                melody = a2m.transcribe (stems::mixGuide (*o.stems), capture.sampleRate).notes;
                bass = a2m.transcribe ((*o.stems)[stems::Stem::bass], capture.sampleRate, mono).notes;
                o.vocals = a2m.transcribe ((*o.stems)[stems::Stem::vocals], capture.sampleRate).notes;
            }
            else
            {
                const auto t = a2m.transcribe (capture.buffer, capture.sampleRate);
                if (! t.ok())
                {
                    o.error = t.error;
                    return std::move (o);
                }
                jaudios::splitBass (t.notes, 48, melody, bass);
            }
            o.melodyNotes = (int) melody.size();
            o.bassNotes = (int) bass.size();
            o.source = jaudios::makeHarmonicSource (o.analysis, capture.durationSeconds(), melody, bass);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            o.progressions = gen::suggest (o.source, engine);
            o.engineMs = juce::Time::getMillisecondCounterHiRes() - t0;
            if (! o.progressions.empty())
                for (int slot = 0; slot < std::min<int> (4, (int) o.progressions[0].chords.size()); ++slot)
                    o.alts.push_back (gen::alternatives (o.source, o.progressions[0], slot, engine));
            return std::move (o);
        },
        [&] (HarmonyRun r) { result = std::move (r); });
    service.waitUntilIdle();
    if (! result || result->error.isNotEmpty() || result->progressions.empty())
    {
        std::cerr << "error: " << (result && result->error.isNotEmpty() ? result->error : juce::String ("no progression (no bars found)")) << "\n";
        return std::nullopt;
    }
    return result;
}

int cmdChords (const juce::File& file, ChordOptions opt)
{
    using namespace jaudios::analysis;
    namespace gen = jaudios::generators;
    double importMs = 0.0;
    const auto capture = importOrReport (file, importMs);
    if (! capture)
        return 1;
    auto result = runHarmony (*capture, opt.engine, opt.stems, opt.useCache);
    if (! result)
        return 1;
    const auto& separation = result->separation;

    const auto key = result->source.key;
    const auto& top = result->progressions.front();
    juce::MidiMessageSequence seq;
    for (const auto& n : gen::render (top, opt.render))
    {
        seq.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) n.velocity).withTimeStamp (n.startSec));
        seq.addEvent (juce::MidiMessage::noteOff (1, n.pitch).withTimeStamp (n.endSec));
    }
    seq.sort();
    seq.updateMatchedPairs();
    if (opt.out == juce::File())
        opt.out = file.getSiblingFile (file.getFileNameWithoutExtension() + ".chords.mid");
    const double bpm = result->analysis.rhythm.bpm > 0.0 ? result->analysis.rhythm.bpm : 120.0;
    if (! writeMidiFile (opt.out, seq, bpm))
    {
        std::cerr << "error: cannot write " << opt.out.getFullPathName() << "\n";
        return 1;
    }

    auto names = [&] (const gen::Progression& p) {
        juce::Array<var> a;
        for (const auto& c : p.chords)
            a.add (juce::String (c.name (key)));
        return a;
    };
    juce::Array<var> progressions;
    for (const auto& p : result->progressions)
        progressions.add (makeObject ({
            { "summary", juce::String (p.summary (key)) },
            { "source", juce::String (p.source) },
            { "score", p.score.total },
            { "parts", makeObject ({ { "melody", p.score.melody }, { "bass", p.score.bass }, { "chroma", p.score.chroma },
                                     { "prior", p.score.prior }, { "voice_leading", p.score.voiceLeading } }) },
            { "max_voice_leading", p.maxVoiceLeading },
            { "chords", names (p) },
        }));
    juce::Array<var> alts;
    for (size_t slot = 0; slot < result->alts.size(); ++slot)
    {
        juce::Array<var> a;
        for (const auto& x : result->alts[slot])
            a.add (makeObject ({ { "chord", juce::String (x.chord.name (key)) }, { "roman", juce::String (x.roman) }, { "score", x.score } }));
        alts.add (makeObject ({ { "slot", (int) slot }, { "current", juce::String (top.chords[slot].name (key)) }, { "alternatives", a } }));
    }
    printJson (makeObject ({
        { "file", file.getFullPathName() },
        { "key", juce::String (jaudios::theory::traditionalName (key)) },
        { "key_margin", result->source.keyMargin },
        { "style", jaudios::generators::styleName (opt.engine.style) },
        { "bars", (int) result->source.bars.size() },
        { "bpm", bpm },
        { "evidence", makeObject ({ { "separation", separation }, { "melody_notes", result->melodyNotes }, { "bass_notes", result->bassNotes } }) },
        { "engine_ms", result->engineMs },
        { "progressions", progressions },
        { "alternatives", alts },
        { "midi_file", opt.out.getFullPathName() },
        { "import_ms", importMs },
    }));
    return 0;
}

struct BassOptions
{
    jaudios::generators::EngineSettings engine;
    std::string pattern;
    jaudios::generators::BassSettings bass;
    bool stems = false, useCache = true;
    juce::File out;
};

int cmdListPatterns()
{
    juce::Array<var> list;
    for (const auto& p : jaudios::generators::builtinBassPatterns())
        list.add (makeObject ({ { "name", juce::String (p.name) }, { "style", juce::String (p.style) }, { "grid", p.grid },
                                { "bars", p.bars }, { "swing", p.swing }, { "steps", (int) p.steps.size() },
                                { "description", juce::String (p.description) } }));
    printJson (makeObject ({ { "patterns", list } }));
    return 0;
}

int cmdBass (const juce::File& file, BassOptions opt)
{
    using namespace jaudios::analysis;
    namespace gen = jaudios::generators;
    double importMs = 0.0;
    const auto capture = importOrReport (file, importMs);
    if (! capture)
        return 1;

    const gen::BassPattern* pattern = opt.pattern.empty() ? gen::bassPatternsFor (opt.engine.style).front()
                                                          : gen::findBassPattern (opt.pattern);
    if (pattern == nullptr)
    {
        std::cerr << "error: no bass pattern '" << opt.pattern << "' (app-cli bass --list-patterns)\n";
        return 2;
    }
    auto harmony = runHarmony (*capture, opt.engine, opt.stems, opt.useCache);
    if (! harmony)
        return 1;

    // kicks from the drums stem (--stems), else from the mix's low band
    juce::AudioBuffer<float> mono (1, capture->buffer.getNumSamples());
    mono.clear();
    const auto& kickSource = harmony->stems ? (*harmony->stems)[stems::Stem::drums] : capture->buffer;
    for (int ch = 0; ch < kickSource.getNumChannels(); ++ch)
        mono.addFrom (0, 0, kickSource, ch, 0, mono.getNumSamples(), 1.0f / (float) kickSource.getNumChannels());

    gen::BassInput in;
    in.key = harmony->source.key;
    for (const auto& b : harmony->source.bars)
        in.bars.push_back ({ b.startSec, b.endSec });
    in.progression = harmony->progressions.front();
    in.kicks = detectKicks (mono.getReadPointer (0), mono.getNumSamples(), capture->sampleRate);
    for (const auto& v : harmony->vocals)
        in.vocals.push_back ({ v.startSec, v.endSec, v.pitch, v.confidence });
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto notes = gen::generateBass (in, *pattern, opt.bass);
    const double genMs = juce::Time::getMillisecondCounterHiRes() - t0;

    juce::MidiMessageSequence seq;
    int sidechained = 0, chordTones = 0;
    juce::Array<var> list;
    for (const auto& n : notes)
    {
        seq.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) n.velocity).withTimeStamp (n.startSec));
        seq.addEvent (juce::MidiMessage::noteOff (1, n.pitch).withTimeStamp (n.endSec));
        sidechained += n.sidechain ? 1 : 0;
        chordTones += in.progression.chords[(size_t) n.slot].contains (n.pitch % 12) ? 1 : 0;
        list.add (makeObject ({ { "start", n.startSec }, { "end", n.endSec }, { "pitch", n.pitch }, { "velocity", n.velocity },
                                { "role", gen::roleName (n.role) }, { "chord", juce::String (in.progression.chords[(size_t) n.slot].name (in.key)) },
                                { "sidechain", n.sidechain } }));
    }
    seq.sort();
    seq.updateMatchedPairs();
    if (opt.out == juce::File())
        opt.out = file.getSiblingFile (file.getFileNameWithoutExtension() + ".bass.mid");
    const double bpm = harmony->analysis.rhythm.bpm > 0.0 ? harmony->analysis.rhythm.bpm : 120.0;
    if (! writeMidiFile (opt.out, seq, bpm))
    {
        std::cerr << "error: cannot write " << opt.out.getFullPathName() << "\n";
        return 1;
    }
    printJson (makeObject ({
        { "file", file.getFullPathName() },
        { "key", juce::String (jaudios::theory::traditionalName (in.key)) },
        { "progression", juce::String (in.progression.summary (in.key)) },
        { "pattern", juce::String (pattern->name) },
        { "grid", opt.bass.gridOverride.value_or (pattern->grid) },
        { "swing", opt.bass.swingOverride.value_or (pattern->swing) },
        { "kicks", (int) in.kicks.size() },
        { "kick_source", harmony->stems ? "drums stem" : "mix low band" },
        { "low_vocal_notes", (int) std::count_if (in.vocals.begin(), in.vocals.end(), [&] (const gen::SourceNote& v) { return v.pitch < opt.bass.vocalLowMidi; }) },
        { "note_count", (int) notes.size() },
        { "chord_tone_share", notes.empty() ? 0.0 : (double) chordTones / (double) notes.size() },
        { "sidechain_notes", sidechained },
        { "bpm", bpm },
        { "generate_ms", genMs },
        { "midi_file", opt.out.getFullPathName() },
        { "notes", list },
        { "import_ms", importMs },
    }));
    return 0;
}

struct DrumOptions
{
    jaudios::generators::Style style = jaudios::generators::Style::house;
    std::string pattern;
    jaudios::generators::DrumSettings drums;
    juce::File kitDir, out;
    bool split = false;
};

int cmdListDrumPatterns()
{
    juce::Array<var> list;
    for (const auto& p : jaudios::generators::builtinDrumPatterns())
    {
        juce::StringArray elements;
        for (int e = 0; e < jaudios::generators::numDrumElements; ++e)
            if (! p.lanes[(size_t) e].empty())
                elements.add (jaudios::generators::elementName ((jaudios::generators::DrumElement) e));
        list.add (makeObject ({ { "name", juce::String (p.name) }, { "style", juce::String (p.style) }, { "bars", p.bars },
                                { "swing", p.swing }, { "elements", elements.joinIntoString (" ") },
                                { "description", juce::String (p.description) } }));
    }
    printJson (makeObject ({ { "patterns", list } }));
    return 0;
}

int cmdDrums (const juce::File& file, DrumOptions opt)
{
    using namespace jaudios::analysis;
    namespace gen = jaudios::generators;
    double importMs = 0.0;
    const auto capture = importOrReport (file, importMs);
    if (! capture)
        return 1;
    const gen::DrumPattern* pattern = opt.pattern.empty() ? gen::drumPatternsFor (opt.style).front() : gen::findDrumPattern (opt.pattern);
    if (pattern == nullptr)
    {
        std::cerr << "error: no drum pattern '" << opt.pattern << "' (app-cli drums --list-patterns)\n";
        return 2;
    }

    // the beat grid of the source (bars between detected downbeats), on the worker like the app
    jaudios::AnalysisService service;
    std::optional<RhythmResult> rhythm;
    service.submit<RhythmResult> (
        [&] {
            juce::AudioBuffer<float> mono (1, capture->buffer.getNumSamples());
            mono.clear();
            for (int ch = 0; ch < capture->buffer.getNumChannels(); ++ch)
                mono.addFrom (0, 0, capture->buffer, ch, 0, mono.getNumSamples(), 1.0f / (float) capture->buffer.getNumChannels());
            return analyzeRhythm (mono.getReadPointer (0), mono.getNumSamples(), capture->sampleRate);
        },
        [&] (RhythmResult r) { rhythm = std::move (r); });
    service.waitUntilIdle();

    std::vector<std::pair<double, double>> bars;
    for (const auto& b : rhythm->bars)
        bars.push_back ({ b.startSec, b.endSec });
    if (bars.empty() && rhythm->beats.size() >= 5)
        for (size_t i = 0; i + 4 < rhythm->beats.size(); i += 4)
            bars.push_back ({ rhythm->beats[i], rhythm->beats[i + 4] });
    if (bars.empty())
    {
        std::cerr << "error: no beat grid found in " << file.getFileName() << "\n";
        return 1;
    }

    const auto notes = gen::generateDrums (bars, *pattern, opt.drums);
    const double sr = capture->sampleRate;
    jaudios::synth::DrumKit kit;
    if (opt.kitDir != juce::File())
    {
        juce::String err;
        auto loaded = jaudios::synth::DrumKit::loadFolder (opt.kitDir, sr, err);
        if (! loaded)
        {
            std::cerr << "error: " << err << "\n";
            return 1;
        }
        kit = std::move (*loaded);
    }
    else
        kit = jaudios::synth::DrumKit::synthesised (sr);

    // kick on the one vs the detected downbeats (skill acceptance: within 5 ms)
    double worstKickMs = 0.0;
    int kicksOnDownbeats = 0;
    for (const auto& n : notes)
        if (n.element == gen::DrumElement::kick && n.step == 0)
        {
            double best = 1e9;
            for (double d : rhythm->downbeats)
                best = std::min (best, std::abs (n.startSec - d));
            for (const auto& b : bars)
                best = std::min (best, std::abs (n.startSec - b.first));
            worstKickMs = std::max (worstKickMs, best * 1000.0);
            ++kicksOnDownbeats;
        }

    if (opt.out == juce::File())
        opt.out = file.getSiblingFile (file.getFileNameWithoutExtension() + ".drums");
    opt.out.createDirectory();
    const int length = (int) std::ceil ((bars.back().second + 2.0) * sr);
    const double bpm = rhythm->bpm > 0.0 ? rhythm->bpm : 120.0;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    bool ok = true;
    juce::Array<var> files;
    auto exportOne = [&] (const juce::String& stem, std::optional<gen::DrumElement> only) {
        const auto wavFile = opt.out.getChildFile (stem + ".wav"), midFile = opt.out.getChildFile (stem + ".mid");
        writeWav (wavFile, jaudios::synth::renderDrums (notes, kit, sr, length, only), sr, ok);
        ok = writeMidiFile (midFile, jaudios::synth::drumsToMidi (notes, only), bpm) && ok;
        int count = 0;
        for (const auto& n : notes)
            count += (! only || n.element == *only) ? 1 : 0;
        files.add (makeObject ({ { "stem", stem }, { "notes", count }, { "wav", wavFile.getFullPathName() }, { "midi", midFile.getFullPathName() } }));
    };
    exportOne ("drums", std::nullopt);
    if (opt.split)
        for (int e = 0; e < gen::numDrumElements; ++e)
        {
            const auto el = (gen::DrumElement) e;
            if (std::any_of (notes.begin(), notes.end(), [&] (const gen::DrumNote& n) { return n.element == el; }))
                exportOne (gen::elementName (el), el);
        }
    if (! ok)
    {
        std::cerr << "error: export to " << opt.out.getFullPathName() << " failed\n";
        return 1;
    }

    juce::Array<var> perElement;
    for (int e = 0; e < gen::numDrumElements; ++e)
    {
        const auto el = (gen::DrumElement) e;
        const auto c = std::count_if (notes.begin(), notes.end(), [&] (const gen::DrumNote& n) { return n.element == el; });
        if (c > 0)
            perElement.add (makeObject ({ { "element", gen::elementName (el) }, { "notes", (int) c } }));
    }
    printJson (makeObject ({
        { "file", file.getFullPathName() },
        { "pattern", juce::String (pattern->name) },
        { "style", juce::String (pattern->style) },
        { "kit", kit.name },
        { "bpm", bpm },
        { "bars", (int) bars.size() },
        { "intensity", opt.drums.intensity },
        { "kick", opt.drums.includeKick },
        { "transition", opt.drums.transitions ? var (gen::variantName (opt.drums.transition)) : var ("none") },
        { "note_count", (int) notes.size() },
        { "elements", perElement },
        { "kicks_on_downbeats", kicksOnDownbeats },
        { "kick_downbeat_max_error_ms", worstKickMs },
        { "render_ms", juce::Time::getMillisecondCounterHiRes() - t0 },
        { "files", files },
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

    if (cmd == "bass" && argc >= 3 && juce::String (argv[2]) == "--list-patterns")
        return cmdListPatterns();
    if (cmd == "drums" && argc >= 3 && juce::String (argv[2]) == "--list-patterns")
        return cmdListDrumPatterns();

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

    if (cmd == "transcribe")
    {
        TranscribeOptions opt;
        for (int i = 3; i < argc; ++i)
        {
            const juce::String a (argv[i]);
            const bool hasValue = i + 1 < argc;
            if (a == "--no-cache") opt.useCache = false;
            else if (a == "--mono") opt.monophonic = true;
            else if (a == "--poly") opt.monophonic = false;
            else if (a == "--stem" && hasValue) opt.stem = juce::String (argv[++i]).toLowerCase();
            else if (a == "--out" && hasValue) opt.out = argFile (argv[++i]);
            else if (a == "--onset" && hasValue) opt.notes.onsetThreshold = juce::String (argv[++i]).getDoubleValue();
            else if (a == "--frame" && hasValue) opt.notes.frameThreshold = juce::String (argv[++i]).getDoubleValue();
            else if (a == "--min-ms" && hasValue) opt.notes.minNoteMs = juce::String (argv[++i]).getDoubleValue();
            else if (a == "--quantize" && hasValue)
            {
                const juce::String q (argv[++i]);
                opt.quantize = q == "off" ? 0 : q.getIntValue();
                if (opt.quantize != 0 && opt.quantize != 4 && opt.quantize != 8 && opt.quantize != 16 && opt.quantize != 32)
                    return usage();
            }
            else
                return usage();
        }
        return cmdTranscribe (file, opt);
    }

    if (cmd == "chords")
    {
        namespace gen = jaudios::generators;
        ChordOptions opt;
        for (int i = 3; i < argc; ++i)
        {
            const juce::String a (argv[i]);
            const bool hasValue = i + 1 < argc;
            if (a == "--sevenths") opt.engine.sevenths = true;
            else if (a == "--stems") opt.stems = true;
            else if (a == "--no-cache") opt.useCache = false;
            else if (a == "--style" && hasValue)
            {
                const auto s = gen::styleFromName (argv[++i]);
                if (! s)
                    return usage();
                opt.engine.style = *s;
            }
            else if (a == "--rhythm" && hasValue)
            {
                const juce::String r (argv[++i]);
                if (r == "1") opt.engine.rhythm = gen::HarmonicRhythm::onePerBar;
                else if (r == "2") opt.engine.rhythm = gen::HarmonicRhythm::twoPerBar;
                else if (r == "half" || r == "0.5") opt.engine.rhythm = gen::HarmonicRhythm::onePerTwoBars;
                else return usage();
            }
            else if (a == "--voicing" && hasValue)
            {
                const juce::String v (argv[++i]);
                if (v == "close") opt.render.voicing = jaudios::theory::Voicing::close;
                else if (v == "open") opt.render.voicing = jaudios::theory::Voicing::open;
                else if (v == "drop2") opt.render.voicing = jaudios::theory::Voicing::drop2;
                else return usage();
            }
            else if (a == "--top" && hasValue) opt.engine.topN = juce::jlimit (1, 32, juce::String (argv[++i]).getIntValue());
            else if (a == "--seed" && hasValue) opt.render.seed = opt.engine.seed = (uint32_t) juce::String (argv[++i]).getLargeIntValue();
            else if (a == "--humanize" && hasValue) opt.render.humanize = juce::jlimit (0, 32, juce::String (argv[++i]).getIntValue());
            else if (a == "--out" && hasValue) opt.out = argFile (argv[++i]);
            else return usage();
        }
        return cmdChords (file, opt);
    }

    if (cmd == "bass")
    {
        namespace gen = jaudios::generators;
        BassOptions opt;
        for (int i = 3; i < argc; ++i)
        {
            const juce::String a (argv[i]);
            const bool hasValue = i + 1 < argc;
            if (a == "--stems") opt.stems = true;
            else if (a == "--no-cache") opt.useCache = false;
            else if (a == "--style" && hasValue)
            {
                const auto st = gen::styleFromName (argv[++i]);
                if (! st)
                    return usage();
                opt.engine.style = *st;
            }
            else if (a == "--pattern" && hasValue) opt.pattern = argv[++i];
            else if (a == "--grid" && hasValue)
            {
                const int g = juce::String (argv[++i]).getIntValue();
                if (g != 8 && g != 16 && g != 32)
                    return usage();
                opt.bass.gridOverride = g;
            }
            else if (a == "--swing" && hasValue) opt.bass.swingOverride = juce::jlimit (0.0, 0.75, juce::String (argv[++i]).getDoubleValue());
            else if (a == "--out" && hasValue) opt.out = argFile (argv[++i]);
            else return usage();
        }
        return cmdBass (file, opt);
    }

    if (cmd == "drums")
    {
        namespace gen = jaudios::generators;
        DrumOptions opt;
        for (int i = 3; i < argc; ++i)
        {
            const juce::String a (argv[i]);
            const bool hasValue = i + 1 < argc;
            if (a == "--no-kick") opt.drums.includeKick = false;
            else if (a == "--split") opt.split = true;
            else if (a == "--style" && hasValue)
            {
                const auto st = gen::styleFromName (argv[++i]);
                if (! st)
                    return usage();
                opt.style = *st;
            }
            else if (a == "--pattern" && hasValue) opt.pattern = argv[++i];
            else if (a == "--intensity" && hasValue) opt.drums.intensity = juce::jlimit (1, 5, juce::String (argv[++i]).getIntValue());
            else if (a == "--phrase" && hasValue) opt.drums.phraseBars = juce::jlimit (1, 64, juce::String (argv[++i]).getIntValue());
            else if (a == "--swing" && hasValue) opt.drums.swingOverride = juce::jlimit (0.0, 0.75, juce::String (argv[++i]).getDoubleValue());
            else if (a == "--kit" && hasValue) opt.kitDir = argFile (argv[++i]);
            else if (a == "--out" && hasValue) opt.out = argFile (argv[++i]);
            else if (a == "--transition" && hasValue)
            {
                const juce::String t (argv[++i]);
                if (t == "none")
                    opt.drums.transitions = false;
                else if (const auto v = gen::variantFromName (t))
                    opt.drums.transition = *v;
                else
                    return usage();
            }
            else return usage();
        }
        return cmdDrums (file, opt);
    }

    return usage();
}
