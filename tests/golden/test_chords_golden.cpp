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
