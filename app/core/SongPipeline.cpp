#include "SongPipeline.h"

#include "HarmonySource.h"

#include "engine/analysis/stems/StemCache.h"
#include "engine/analysis/tempo/KickDetector.h"

namespace jaudios
{

using namespace analysis;
namespace gen = generators;
namespace tr = analysis::transcription;

StemRun separateStems (const CaptureResult& capture, bool useCache, const std::function<void (double)>& progress)
{
    StemRun run;
    const auto config = AppConfig::fromEnvironment();
    const auto model = findModelFile (config, "htdemucs.onnx");
    if (model == juce::File())
    {
        run.error = "htdemucs.onnx not found (python3 tools/models.py --only demucs --python <env>, or set JAUDIOS_MODELS_DIR)";
        return run;
    }
    const auto cacheDirEnv = juce::SystemStats::getEnvironmentVariable ("JAUDIOS_STEM_CACHE_DIR", {});
    const stems::StemCache cache (cacheDirEnv.isNotEmpty() ? juce::File (cacheDirEnv) : stems::StemCache::defaultDirectory());

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    stems::StemSeparator sep ({ model, config.onnxCoreML });
    run.loadMs = juce::Time::getMillisecondCounterHiRes() - t0;
    run.provider = sep.executionProvider();
    run.origin = sep.modelOrigin();
    if (! sep.isReady())
    {
        run.error = sep.loadError();
        return run;
    }
    run.cacheKey = stems::StemCache::keyFor (capture.buffer, capture.sampleRate, sep.cacheId());
    if (useCache)
        if (auto cached = cache.load (run.cacheKey))
        {
            run.cacheHit = true;
            run.stems = std::move (cached);
            return run;
        }
    auto r = sep.separate (capture.buffer, capture.sampleRate, progress ? progress : [] (double) {});
    if (! r.stems)
    {
        run.error = r.error.isNotEmpty() ? r.error : juce::String ("separation failed");
        return run;
    }
    if (useCache && cache.store (run.cacheKey, *r.stems))
        cache.prune();
    run.segments = r.segments;
    run.separateSec = r.seconds;
    run.stems = std::move (r.stems);
    return run;
}

void suggestProgressions (SongAnalysis& song, const gen::EngineSettings& engine)
{
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    song.progressions = gen::suggest (song.source, engine);
    song.engineMs = juce::Time::getMillisecondCounterHiRes() - t0;
}

std::optional<SongAnalysis> analyseSong (const CaptureResult& capture, const SongSettings& settings, juce::String& error,
                                         IKeyDetector* keyDetector, std::optional<stems::Stems> given, const juce::String& givenLabel)
{
    const auto config = AppConfig::fromEnvironment();
    const auto bpModel = findModelFile (config, "basic_pitch.onnx");
    if (bpModel == juce::File())
        return (error = "basic_pitch.onnx not found (python3 tools/models.py --only basic_pitch, or set JAUDIOS_MODELS_DIR)", std::nullopt);

    SongAnalysis s;
    s.sampleRate = capture.sampleRate;
    s.durationSec = capture.durationSeconds();
    if (given)
    {
        s.stems = std::move (given);
        s.separation = givenLabel.isNotEmpty() ? givenLabel : juce::String ("given");
    }
    else if (settings.useStems)
    {
        auto run = separateStems (capture, settings.useStemCache);
        if (! run.stems)
            return (error = run.error, std::nullopt);
        s.separation = run.cacheHit ? juce::String ("cache") : "htdemucs (" + run.provider + ")";
        s.stems = std::move (run.stems);
    }

    KeyDetectorHandle own;
    if (keyDetector == nullptr)
    {
        own = makeKeyDetector (config);
        keyDetector = own.detector.get();
    }
    s.analysis = analyze (capture, *keyDetector);

    tr::AudioToMidi a2m (bpModel);
    std::vector<tr::Note> melody, bass;
    if (s.stems)
    {
        tr::TranscriptionSettings mono;
        mono.monophonic = true;
        melody = a2m.transcribe (stems::mixGuide (*s.stems), capture.sampleRate).notes;
        bass = a2m.transcribe ((*s.stems)[stems::Stem::bass], capture.sampleRate, mono).notes;
        s.vocals = a2m.transcribe ((*s.stems)[stems::Stem::vocals], capture.sampleRate).notes;
    }
    else
    {
        const auto t = a2m.transcribe (capture.buffer, capture.sampleRate);
        if (! t.ok())
            return (error = t.error, std::nullopt);
        splitBass (t.notes, 48, melody, bass);
    }
    s.melodyNotes = (int) melody.size();
    s.bassNotes = (int) bass.size();
    s.source = makeHarmonicSource (s.analysis, s.durationSec, melody, bass);

    juce::AudioBuffer<float> mono (1, capture.buffer.getNumSamples());
    mono.clear();
    for (int ch = 0; ch < capture.buffer.getNumChannels(); ++ch)
        mono.addFrom (0, 0, capture.buffer, ch, 0, mono.getNumSamples(), 1.0f / (float) capture.buffer.getNumChannels());
    s.kicks = detectKicks (mono.getReadPointer (0), mono.getNumSamples(), capture.sampleRate);

    suggestProgressions (s, settings.engine);
    if (s.progressions.empty())
        return (error = "no progression (no bars found)", std::nullopt);
    return s;
}

std::vector<std::pair<double, double>> songBars (const SongAnalysis& song)
{
    std::vector<std::pair<double, double>> bars;
    for (const auto& b : song.source.bars)
        bars.push_back ({ b.startSec, b.endSec });
    return bars;
}

SongParts generateParts (const SongAnalysis& song, const gen::Progression& progression, gen::Style style, bool withDrums,
                         const gen::BassPattern* bassPattern, const gen::DrumPattern* drumPattern)
{
    SongParts p;
    p.progression = progression;
    p.bassPattern = bassPattern != nullptr ? bassPattern : gen::bassPatternsFor (style).front();
    p.drumPattern = withDrums ? (drumPattern != nullptr ? drumPattern : gen::drumPatternsFor (style).front()) : nullptr;

    gen::BassInput in;
    in.key = song.source.key;
    in.bars = songBars (song);
    in.progression = progression;
    in.kicks = song.kicks;
    for (const auto& v : song.vocals)
        in.vocals.push_back ({ v.startSec, v.endSec, v.pitch, v.confidence });
    p.bassNotes = gen::generateBass (in, *p.bassPattern);
    if (p.drumPattern != nullptr)
    {
        gen::DrumSettings drums;
        drums.includeKick = true;
        p.drumNotes = gen::generateDrums (in.bars, *p.drumPattern, drums);
    }
    p.chordNotes = gen::render (progression);
    for (const auto& n : p.chordNotes)
    {
        p.chords.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) n.velocity).withTimeStamp (n.startSec));
        p.chords.addEvent (juce::MidiMessage::noteOff (1, n.pitch).withTimeStamp (n.endSec));
    }
    for (const auto& n : p.bassNotes)
    {
        p.bass.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) n.velocity).withTimeStamp (n.startSec));
        p.bass.addEvent (juce::MidiMessage::noteOff (1, n.pitch).withTimeStamp (n.endSec));
    }
    p.chords.sort();
    p.bass.sort();
    p.chords.updateMatchedPairs();
    p.bass.updateMatchedPairs();
    return p;
}

} // namespace jaudios
