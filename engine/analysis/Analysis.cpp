#include "Analysis.h"

#include "key/KeyDetectorChroma.h"

namespace jaudios::analysis
{

AnalysisResult analyze (const CaptureResult& capture, IKeyDetector& keyDetector)
{
    AnalysisResult r;
    const auto& buffer = capture.buffer;

    juce::AudioBuffer<float> mono (1, buffer.getNumSamples());
    mono.clear();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        mono.addFrom (0, 0, buffer, ch, 0, buffer.getNumSamples(), 1.0f / (float) std::max (1, buffer.getNumChannels()));

    auto t0 = juce::Time::getMillisecondCounterHiRes();
    r.rhythm = analyzeRhythm (mono.getReadPointer (0), mono.getNumSamples(), capture.sampleRate);
    r.rhythmMs = juce::Time::getMillisecondCounterHiRes() - t0;

    t0 = juce::Time::getMillisecondCounterHiRes();
    if (auto* chromaDetector = dynamic_cast<KeyDetectorChroma*> (&keyDetector); chromaDetector != nullptr && ! r.rhythm.chroma.frames.empty())
        r.key = KeyDetectorChroma::analyzeFrames (r.rhythm.chroma); // same HPSS chroma, computed once
    else
        r.key = keyDetector.analyzeBuffer (buffer, capture.sampleRate);
    r.keyMs = juce::Time::getMillisecondCounterHiRes() - t0;

    return r;
}

namespace
{
juce::var object() { return juce::var (new juce::DynamicObject()); }

juce::var array (const std::vector<double>& v, int decimals = 4)
{
    juce::Array<juce::var> a;
    for (double x : v)
        a.add (juce::String (x, decimals).getDoubleValue());
    return a;
}

double round (double x, int decimals)
{
    const double p = std::pow (10.0, decimals);
    return std::round (x * p) / p;
}
} // namespace

juce::var toJson (const AnalysisResult& r, const CaptureResult& c, const juce::String& keyWarning)
{
    auto root = object();
    auto* o = root.getDynamicObject();

    auto source = object();
    source.getDynamicObject()->setProperty ("file", c.sourceFile.getFullPathName());
    source.getDynamicObject()->setProperty ("duration_sec", round (c.durationSeconds(), 3));
    source.getDynamicObject()->setProperty ("original_sample_rate", c.originalSampleRate);
    source.getDynamicObject()->setProperty ("original_channels", c.originalNumChannels);
    o->setProperty ("source", source);

    // --- key
    auto key = object();
    auto* k = key.getDynamicObject();
    if (! r.key.top3.empty())
    {
        const auto& best = r.key.top3[0].key;
        k->setProperty ("traditional", juce::String (theory::traditionalName (best)));
        k->setProperty ("camelot", juce::String (theory::camelot (best)));
        k->setProperty ("openkey", juce::String (theory::openKey (best)));
    }
    k->setProperty ("margin", round (r.key.margin, 4));
    k->setProperty ("low_confidence", r.key.lowConfidence);
    k->setProperty ("backend", juce::String (toString (r.key.backend)));
    k->setProperty ("scores_available", r.key.scoresAvailable);
    if (keyWarning.isNotEmpty() || r.key.warning.isNotEmpty())
        k->setProperty ("warning", r.key.warning.isNotEmpty() ? r.key.warning : keyWarning);
    if (r.key.ensembleChroma != nullptr && ! r.key.ensembleChroma->top3.empty())
    {
        k->setProperty ("ensemble_chroma", juce::String (theory::camelot (r.key.ensembleChroma->top3[0].key)));
        k->setProperty ("chroma_agrees", r.key.chromaAgrees.value_or (false));
    }

    juce::Array<juce::var> top3;
    for (const auto& cand : r.key.top3)
    {
        auto e = object();
        e.getDynamicObject()->setProperty ("traditional", juce::String (theory::traditionalName (cand.key)));
        e.getDynamicObject()->setProperty ("camelot", juce::String (theory::camelot (cand.key)));
        e.getDynamicObject()->setProperty ("openkey", juce::String (theory::openKey (cand.key)));
        e.getDynamicObject()->setProperty ("score", round (cand.score, 4));
        top3.add (e);
    }
    k->setProperty ("top3", top3);
    k->setProperty ("tuning_cents", round (r.rhythm.tuningCents, 1));
    k->setProperty ("elapsed_ms", round (r.keyMs, 1));
    o->setProperty ("key", key);

    // --- tempo / grid
    auto tempo = object();
    auto* t = tempo.getDynamicObject();
    t->setProperty ("bpm", round (r.rhythm.bpm, 2));
    t->setProperty ("autocorr_bpm", round (r.rhythm.autocorrBpm, 2));
    t->setProperty ("confidence", round (r.rhythm.tempoConfidence, 2));
    t->setProperty ("aliases", array (r.rhythm.tempoAliases, 2));
    t->setProperty ("fixed_tempo", r.rhythm.fixedTempo);
    t->setProperty ("grid_residual_ms", round (r.rhythm.gridResidualMs, 2));
    t->setProperty ("beats_per_bar", r.rhythm.beatsPerBar);
    t->setProperty ("num_beats", (int) r.rhythm.beats.size());
    t->setProperty ("num_bars", (int) r.rhythm.bars.size());
    t->setProperty ("first_beat_sec", r.rhythm.beats.empty() ? juce::var() : juce::var (round (r.rhythm.beats.front(), 4)));
    t->setProperty ("first_downbeat_sec", r.rhythm.downbeats.empty() ? juce::var() : juce::var (round (r.rhythm.downbeats.front(), 4)));
    t->setProperty ("elapsed_ms", round (r.rhythmMs, 1));
    o->setProperty ("tempo", tempo);

    o->setProperty ("beats", array (r.rhythm.beats));
    o->setProperty ("downbeats", array (r.rhythm.downbeats));

    static const char* pcNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    juce::Array<juce::var> bars;
    for (const auto& b : r.rhythm.bars)
    {
        auto e = object();
        e.getDynamicObject()->setProperty ("start", round (b.startSec, 4));
        e.getDynamicObject()->setProperty ("end", round (b.endSec, 4));
        juce::Array<juce::var> chroma;
        for (float v : b.chroma)
            chroma.add (round (v, 3));
        e.getDynamicObject()->setProperty ("chroma", chroma);

        juce::StringArray top;
        std::array<int, 12> order {};
        std::iota (order.begin(), order.end(), 0);
        std::stable_sort (order.begin(), order.end(), [&] (int x, int y) { return b.chroma[(size_t) x] > b.chroma[(size_t) y]; });
        for (int i = 0; i < 3; ++i)
            top.add (pcNames[order[(size_t) i]]);
        e.getDynamicObject()->setProperty ("top_pitch_classes", top.joinIntoString (" "));
        bars.add (e);
    }
    o->setProperty ("bars", bars);

    // Not in M1: stem separation and chord suggestions land in later milestones.
    o->setProperty ("stems", juce::var());
    o->setProperty ("chords", juce::var());

    return root;
}

} // namespace jaudios::analysis
