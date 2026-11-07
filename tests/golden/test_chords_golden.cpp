// chord-engine on the fixtures: the full pipeline (key + beat grid + Basic Pitch on the mix +
// ChordEngine) against the chords in <fixture>.truth.json, and the skill's acceptance on the C major
// fixture. Needs build/models/basic_pitch.onnx; the key model is optional (chroma fallback).
#include "app/core/HarmonySource.h"
#include "engine/analysis/AudioImport.h"
#include "engine/analysis/key/KeyDetector.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <set>

using namespace jaudios;
namespace gen = jaudios::generators;
namespace tr = jaudios::analysis::transcription;

namespace
{
juce::File devModel() { return juce::File (JAUDIOS_DEV_MODELS_DIR).getChildFile ("basic_pitch.onnx"); }
juce::File fixture (const juce::String& name, const char* ext) { return juce::File (JAUDIOS_SOURCE_DIR).getChildFile ("tests/fixtures/" + name + ext); }

struct Run
{
    gen::HarmonicSource source;
    std::vector<gen::Progression> progressions;
};

Run run (const juce::String& name)
{
    const analysis::AudioImporter importer;
    const auto imported = importer.importFile (fixture (name, ".wav"));
    REQUIRE (imported.ok());
    const auto& capture = *imported.capture;

    auto config = analysis::AppConfig::fromEnvironment();
    config.modelsDir = juce::File (JAUDIOS_DEV_MODELS_DIR);
    auto detector = analysis::makeKeyDetector (config);
    const auto analysis = analysis::analyze (capture, *detector.detector);

    static tr::AudioToMidi a2m (devModel());
    const auto t = a2m.transcribe (capture.buffer, capture.sampleRate);
    REQUIRE (t.ok());
    std::vector<tr::Note> melody, bass;
    splitBass (t.notes, 48, melody, bass);

    Run r;
    r.source = makeHarmonicSource (analysis, capture.durationSeconds(), melody, bass);
    r.progressions = gen::suggest (r.source);
    REQUIRE_FALSE (r.progressions.empty());
    return r;
}

/** Truth chord names ("Am", "Bbm", "F") as root + major/minor triad. */
theory::Chord truthChord (const juce::String& name)
{
    const auto k = theory::keyFromName (name.toStdString());
    REQUIRE (k.has_value());
    return { k->tonic, k->mode == theory::Mode::minor ? theory::Quality::min : theory::Quality::maj };
}

/** Fraction of truth bars whose centre falls in a slot holding the truth chord (root + quality). */
double barAccuracy (const gen::Progression& p, const juce::var& truth)
{
    const double bar = 4.0 * 60.0 / (double) truth["bpm"];
    const double t0 = truth["first_downbeat_sec"];
    const auto* chords = truth["chords_per_bar"].getArray();
    int hit = 0;
    for (int b = 0; b < chords->size(); ++b)
    {
        const double centre = t0 + (b + 0.5) * bar;
        const auto want = truthChord ((*chords)[b].toString());
        for (size_t s = 0; s < p.slots.size(); ++s)
            if (centre >= p.slots[s].startSec && centre < p.slots[s].endSec)
            {
                hit += (p.chords[s].root == want.root && p.chords[s].quality == want.quality) ? 1 : 0;
                break;
            }
    }
    return (double) hit / chords->size();
}

#define REQUIRE_MODEL() if (! devModel().existsAsFile()) SKIP ("no model: python3 tools/models.py --only basic_pitch")
} // namespace

TEST_CASE ("Chord engine top-1 matches the fixtures' chords", "[chords][golden]")
{
    REQUIRE_MODEL();
    const juce::String name = GENERATE (as<juce::String>{}, "pop_100_Cmaj", "house_124_Amin", "techno_132_Fmin", "loop_8bar_Gmaj");
    const auto r = run (name);
    const auto truth = juce::JSON::parse (fixture (name, ".truth.json"));
    const auto& top = r.progressions.front();
    const double acc = barAccuracy (top, truth);
    WARN (name << ": key " << theory::traditionalName (r.source.key) << " (margin " << r.source.keyMargin << "), top-1 "
               << top.summary (r.source.key) << " [" << top.source << "], bar accuracy " << acc);
    CHECK (acc >= 0.9);
    CHECK (top.maxVoiceLeading <= 6);
}

TEST_CASE ("Skill acceptance: C major fixture -> top-1 uses I / IV / V / vi only, voice leading <= 6", "[chords][golden][acceptance]")
{
    REQUIRE_MODEL();
    const auto r = run ("pop_100_Cmaj");
    const auto& top = r.progressions.front();
    const theory::Key cMajor { 0, theory::Mode::major }; // the fixture's key, whatever the detector said
    const std::set<std::string> allowed { "I", "IV", "V", "vi" };
    for (const auto& c : top.chords)
    {
        INFO (c.name (cMajor));
        CHECK (allowed.count (theory::romanOf (cMajor, c)) == 1);
    }
    CHECK (top.maxVoiceLeading <= 6);
    CHECK (r.progressions.size() == 8);
}

//==============================================================================
// bass-generator on the same pipeline: acceptance on real analysis output + kick detection vs
// the fixture renderer's kick positions.
#include "engine/analysis/tempo/KickDetector.h"
#include "engine/generators/BassGenerator.h"

namespace
{
std::vector<float> monoOf (const juce::String& name)
{
    const analysis::AudioImporter importer;
    const auto imported = importer.importFile (fixture (name, ".wav"));
    REQUIRE (imported.ok());
    const auto& b = imported.capture->buffer;
    std::vector<float> m ((size_t) b.getNumSamples(), 0.0f);
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            m[(size_t) i] += b.getSample (ch, i) / (float) b.getNumChannels();
    return m;
}
} // namespace

TEST_CASE ("Bass on the fixtures: every note a chord or scale tone, root on each downbeat", "[bass][golden][acceptance]")
{
    REQUIRE_MODEL();
    const juce::String name = GENERATE (as<juce::String>{}, "pop_100_Cmaj", "house_124_Amin", "techno_132_Fmin", "loop_8bar_Gmaj");
    const auto r = run (name);
    gen::BassInput in;
    in.key = r.source.key;
    for (const auto& b : r.source.bars)
        in.bars.push_back ({ b.startSec, b.endSec });
    in.progression = r.progressions.front();
    const auto scale = theory::scalePitchClasses (in.key);
    for (const auto& pattern : gen::builtinBassPatterns())
    {
        const auto notes = gen::generateBass (in, pattern);
        INFO (name << " / " << pattern.name);
        REQUIRE_FALSE (notes.empty());
        for (const auto& n : notes)
        {
            const auto& chord = in.progression.chords[(size_t) n.slot];
            const bool ok = chord.contains (n.pitch % 12) || std::find (scale.begin(), scale.end(), n.pitch % 12) != scale.end();
            CHECK (ok);
            for (const auto& s : in.progression.slots)
                if (std::abs (n.startSec - s.startSec) < 1e-6)
                    CHECK (n.pitch % 12 == chord.root);
        }
    }
}

TEST_CASE ("Kick detection on the fixture mixes vs the rendered kicks", "[bass][kicks][golden]")
{
    const juce::String name = GENERATE (as<juce::String>{}, "house_124_Amin", "techno_132_Fmin", "pop_100_Cmaj", "loop_8bar_Gmaj");
    const auto truth = juce::JSON::parse (fixture (name, ".truth.json"));
    const double beat = 60.0 / (double) truth["bpm"], t0 = truth["first_downbeat_sec"];
    const int beats = 4 * (int) truth["bars"];
    std::vector<double> expected;
    for (int b = 0; b < beats; ++b)
        if (name != "pop_100_Cmaj" || b % 2 == 0) // pop: kick on 1 and 3 (FixtureRender)
            expected.push_back (t0 + b * beat);

    const auto mono = monoOf (name);
    const auto kicks = analysis::detectKicks (mono.data(), (int) mono.size(), 44100.0);
    int hit = 0;
    for (double e : expected)
        hit += std::any_of (kicks.begin(), kicks.end(), [&] (double k) { return std::abs (k - e) < 0.03; }) ? 1 : 0;
    const double recall = (double) hit / (double) expected.size(), precision = kicks.empty() ? 0.0 : (double) hit / (double) kicks.size();
    WARN (name << " (mix): " << kicks.size() << " kicks, " << expected.size() << " rendered; recall " << recall << ", precision " << precision);
    CHECK (recall >= 0.95);
    // On a mix, bass attacks between the kicks share the kick band (techno: rolling 16ths; pop: bass on
    // every beat, kick on 1 and 3), so precision is gated only where the bass plays with the kicks.
    // The bass generator takes kicks from the drums stem for that reason (next test).
    if (name == "house_124_Amin" || name == "loop_8bar_Gmaj")
        CHECK (precision >= 0.9);
}

#include "engine/analysis/stems/StemSeparator.h"

TEST_CASE ("Kick detection on the drums stem (HT-Demucs) of the pop fixture", "[bass][kicks][golden][stems]")
{
    const auto model = juce::File (JAUDIOS_DEV_MODELS_DIR).getChildFile ("htdemucs.onnx");
    if (! model.existsAsFile())
        SKIP ("no stem model: python3 tools/models.py --only demucs --python <env>");
    const analysis::AudioImporter importer;
    auto imported = importer.importFile (fixture ("pop_100_Cmaj", ".wav"));
    REQUIRE (imported.ok());
    auto mix = imported.capture->buffer;
    mix.setSize (mix.getNumChannels(), 16 * 44100, true, false, false); // first 16 s: 3 segments

    analysis::stems::StemSeparator sep ({ model });
    REQUIRE (sep.isReady());
    const auto r = sep.separate (mix, 44100.0);
    REQUIRE (r.stems);
    const auto& drumsStem = (*r.stems)[analysis::stems::Stem::drums];
    std::vector<float> mono ((size_t) drumsStem.getNumSamples(), 0.0f);
    for (int ch = 0; ch < drumsStem.getNumChannels(); ++ch)
        for (int i = 0; i < drumsStem.getNumSamples(); ++i)
            mono[(size_t) i] += 0.5f * drumsStem.getSample (ch, i);
    const auto kicks = analysis::detectKicks (mono.data(), (int) mono.size(), 44100.0);

    const double beat = 0.6; // 100 BPM, kick on beats 1 and 3
    std::vector<double> expected;
    for (double t = 0.0; t < 16.0 - 0.1; t += 2 * beat)
        expected.push_back (t);
    int hit = 0;
    for (double e : expected)
        hit += std::any_of (kicks.begin(), kicks.end(), [&] (double k) { return std::abs (k - e) < 0.03; }) ? 1 : 0;
    const double recall = (double) hit / (double) expected.size(), precision = kicks.empty() ? 0.0 : (double) hit / (double) kicks.size();
    WARN ("pop_100_Cmaj drums stem: " << kicks.size() << " kicks, " << expected.size() << " rendered; recall " << recall << ", precision " << precision);
    CHECK (recall >= 0.9);
    CHECK (precision >= 0.9);
}

//==============================================================================
// drum-generator acceptance: kicks align to the detected downbeats within 5 ms, in the MIDI and in
// the rendered audio (no model needed: beat grid + generator + synthesised kit).
#include "engine/analysis/tempo/BeatTracker.h"
#include "engine/generators/DrumGenerator.h"
#include "engine/synth/DrumSampler.h"

TEST_CASE ("Skill acceptance: drum kicks align to the detected downbeats within 5 ms (MIDI + audio)", "[drums][golden][acceptance]")
{
    const juce::String name = GENERATE (as<juce::String>{}, "house_124_Amin", "techno_132_Fmin", "pop_100_Cmaj", "loop_8bar_Gmaj");
    const auto mono = monoOf (name);
    const auto rhythm = analysis::analyzeRhythm (mono.data(), (int) mono.size(), 44100.0);
    REQUIRE (rhythm.bars.size() >= 4);
    std::vector<std::pair<double, double>> bars;
    for (const auto& b : rhythm.bars)
        bars.push_back ({ b.startSec, b.endSec });

    for (const auto& pattern : gen::builtinDrumPatterns())
    {
        gen::DrumSettings s; // transitions on: fills and drops must not move the remaining kicks
        s.transition = gen::DrumVariant::riserHit;
        const auto notes = gen::generateDrums (bars, pattern, s);
        const auto audio = synth::renderDrums (notes, synth::DrumKit::synthesised (44100.0), 44100.0,
                                               (int) ((bars.back().second + 1.0) * 44100.0), gen::DrumElement::kick);
        INFO (name << " / " << pattern.name);
        int checked = 0;
        for (const auto& n : notes)
        {
            if (n.element != gen::DrumElement::kick || n.step != 0)
                continue;
            const double downbeat = bars[(size_t) n.bar].first;
            CHECK (std::abs (n.startSec - downbeat) < 0.005); // MIDI
            // audio: the kick's attack is the biggest jump in |x| within +-10 ms of the downbeat
            const int centre = (int) std::lround (downbeat * 44100.0), w = 441;
            int best = centre;
            float bestJump = -1.0f;
            for (int i = std::max (1, centre - w); i < std::min (audio.getNumSamples(), centre + w); ++i)
            {
                const float jump = std::abs (audio.getSample (0, i)) - std::abs (audio.getSample (0, i - 1));
                if (jump > bestJump)
                {
                    bestJump = jump;
                    best = i;
                }
            }
            CHECK (std::abs (best / 44100.0 - downbeat) < 0.005);
            ++checked;
        }
        CHECK (checked > 0);
    }
}
