#include "TestSignals.h"

#include "engine/analysis/key/KeyDetector.h"
#include "engine/analysis/key/KeyDetectorChroma.h"
#include "engine/theory/Key.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace jaudios;
using namespace jaudios::analysis;
using namespace jaudios::theory;

namespace
{
/** I-IV-V-I (major) or i-iv-V-i (harmonic minor), 1 s per chord, twice. */
juce::AudioBuffer<float> cadence (Key key, double tuningA4 = 440.0)
{
    const bool minor = key.mode == Mode::minor;
    const auto notes = test::progression (key.tonic, minor, { 0, 3, 4, 0 }, 1.0, 2);
    return test::renderNotes (notes, 8.5, 44100.0, 6, tuningA4);
}
} // namespace

TEST_CASE ("Chroma key detector finds all 24 keys from a cadence", "[key][chroma]")
{
    const int index = GENERATE (range (0, 24));
    const Key truth = keyFromIndex24 (index);

    KeyDetectorChroma detector;
    const auto result = detector.analyzeBuffer (cadence (truth), 44100.0);

    INFO ("truth " << traditionalName (truth) << ", got " << traditionalName (result.top3.at (0).key));
    CHECK (result.top3.at (0).key == truth);
}

TEST_CASE ("Chroma key detector tolerates detuning and drums", "[key][chroma]")
{
    const Key truth { 9, Mode::minor }; // A minor

    SECTION ("A4 = 446 Hz (+24 cents)")
    {
        KeyDetectorChroma detector;
        CHECK (detector.analyzeBuffer (cadence (truth, 446.0), 44100.0).top3.at (0).key == truth);
    }
    SECTION ("four-on-the-floor kick and hats on top")
    {
        auto audio = cadence (truth);
        test::addFourOnFloor (audio, 120.0, 0.0, 16);
        KeyDetectorChroma detector;
        CHECK (detector.analyzeBuffer (audio, 44100.0).top3.at (0).key == truth);
    }
    SECTION ("48 kHz input is resampled")
    {
        const auto notes = test::progression (truth.tonic, true, { 0, 3, 4, 0 }, 1.0, 2);
        KeyDetectorChroma detector;
        CHECK (detector.analyzeBuffer (test::renderNotes (notes, 8.5, 48000.0), 48000.0).top3.at (0).key == truth);
    }
}

TEST_CASE ("KeyResult invariants", "[key][chroma]")
{
    KeyDetectorChroma detector;
    const auto result = detector.analyzeBuffer (cadence ({ 0, Mode::major }), 44100.0);

    REQUIRE (result.top3.size() == 3);
    CHECK (result.backend == KeyBackend::chroma);
    CHECK (result.scoresAvailable);
    CHECK (result.top3[0].score >= result.top3[1].score);
    CHECK (result.top3[1].score >= result.top3[2].score);
    for (const auto& c : result.top3)
    {
        CHECK (c.score >= 0.0f);
        CHECK (c.score <= 1.0f);
        CHECK (c.camelotNumber == camelotNumber (c.key));
        CHECK (c.camelotLetter == camelotLetter (c.key));
    }
    CHECK_THAT (result.margin, Catch::Matchers::WithinAbs (result.top3[0].score - result.top3[1].score, 1e-6));

    float sum = 0.0f;
    for (float s : result.scores)
        sum += s;
    CHECK_THAT (sum, Catch::Matchers::WithinAbs (1.0, 1e-4)); // softmax over 24 keys
}

TEST_CASE ("Silence yields a low-confidence result instead of a guess", "[key][chroma]")
{
    juce::AudioBuffer<float> silence (2, 44100 * 5);
    silence.clear();

    KeyDetectorChroma detector;
    const auto result = detector.analyzeBuffer (silence, 44100.0);

    CHECK (result.lowConfidence);
    CHECK (result.margin == 0.0f);
}

TEST_CASE ("analyzeFile reads the file and matches analyzeBuffer", "[key][chroma]")
{
    test::TempDir tmp;
    const auto audio = cadence ({ 7, Mode::major }); // G major
    const auto file = tmp.dir.getChildFile ("g.wav");
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (44100.0).withNumChannels (1).withBitsPerSample (32));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }

    KeyDetectorChroma detector;
    const auto fromFile = detector.analyzeFile (file);
    const auto fromBuffer = detector.analyzeBuffer (audio, 44100.0);

    REQUIRE_FALSE (fromFile.top3.empty());
    CHECK (fromFile.top3[0].key == Key { 7, Mode::major });
    CHECK (fromFile.top3[0].key == fromBuffer.top3[0].key);
    CHECK_THAT (fromFile.margin, Catch::Matchers::WithinAbs (fromBuffer.margin, 1e-4));
}

TEST_CASE ("makeKeyDetector honours KEY_BACKEND and falls back to chroma", "[key][factory]")
{
    SECTION ("chroma")
    {
        AppConfig config;
        config.keyBackend = "chroma";
        auto d = makeKeyDetector (config);
        REQUIRE (d.detector != nullptr);
        CHECK (d.backend == KeyBackend::chroma);
        CHECK (d.warning.isEmpty());
    }
    SECTION ("sidecar / onnx are not available in this build")
    {
        for (const char* name : { "sidecar", "onnx" })
        {
            AppConfig config;
            config.keyBackend = name;
            auto d = makeKeyDetector (config);
            REQUIRE (d.detector != nullptr);
            CHECK (d.backend == KeyBackend::chroma);
            CHECK (d.warning.isNotEmpty());
        }
    }
    SECTION ("unknown value")
    {
        AppConfig config;
        config.keyBackend = "banana";
        auto d = makeKeyDetector (config);
        CHECK (d.backend == KeyBackend::chroma);
        CHECK (d.warning.isNotEmpty());
    }
    SECTION ("environment variable")
    {
        CHECK (AppConfig::fromEnvironment ({ { "KEY_BACKEND", "onnx" } }).keyBackend == "onnx");
        CHECK (AppConfig::fromEnvironment ({}).keyBackend == "chroma");
    }
}

TEST_CASE ("Relative-key loops resolve to the chord they open on", "[key][chroma][relative]")
{
    // vi-IV-I-V and I-V-vi-IV contain the same pitch classes; the opening chord is the tonic cue.
    const int majorTonic = GENERATE (0, 7, 3); // C, G, Eb
    const double chordSec = 2.0;

    SECTION ("opens on vi: minor")
    {
        const auto notes = test::progression (majorTonic, false, { 5, 3, 0, 4 }, chordSec, 4);
        KeyDetectorChroma detector;
        const auto r = detector.analyzeBuffer (test::renderNotes (notes, 16 * chordSec + 0.5), 44100.0);
        INFO ("got " << traditionalName (r.top3[0].key));
        CHECK (r.top3[0].key == relative (Key { majorTonic, Mode::major }));
    }
    SECTION ("opens on I: major")
    {
        const auto notes = test::progression (majorTonic, false, { 0, 4, 5, 3 }, chordSec, 4);
        KeyDetectorChroma detector;
        const auto r = detector.analyzeBuffer (test::renderNotes (notes, 16 * chordSec + 0.5), 44100.0);
        INFO ("got " << traditionalName (r.top3[0].key));
        CHECK (r.top3[0].key == Key { majorTonic, Mode::major });
    }
}

TEST_CASE ("Opening-tonic evidence ignores a silent or drum-only intro", "[key][chroma][relative]")
{
    // 4 s of drums only, then vi-IV-I-V in G (Em C G D): still E minor.
    auto notes = test::progression (7, false, { 5, 3, 0, 4 }, 2.0, 3);
    for (auto& n : notes)
        n.start += 4.0;
    auto audio = test::renderNotes (notes, 4.0 + 24.0 + 0.5);
    test::addFourOnFloor (audio, 120.0, 0.0, 56);

    KeyDetectorChroma detector;
    const auto r = detector.analyzeBuffer (audio, 44100.0);
    INFO ("got " << traditionalName (r.top3[0].key));
    CHECK (r.top3[0].key == Key { 4, Mode::minor });
}
