// C++ side of ml/tests/goldens.json (see ml-model-pipeline skill). Each fixture that exists in
// tests/fixtures/ is imported, analysed with the PR-CI key backends, and checked against the JSON.

#include "engine/analysis/Analysis.h"
#include "engine/analysis/AudioImport.h"
#include "engine/analysis/key/KeyDetector.h"
#include "engine/theory/Key.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace jaudios;
using namespace jaudios::analysis;

namespace
{
juce::File sourceDir() { return juce::File (JAUDIOS_SOURCE_DIR); }

juce::var loadGoldens()
{
    return juce::JSON::parse (sourceDir().getChildFile ("ml/tests/goldens.json"));
}

std::vector<juce::var> fixtures()
{
    std::vector<juce::var> out;
    const auto goldens = loadGoldens();
    const auto list = goldens["fixtures"];
    if (auto* arr = list.getArray())
        for (const auto& f : *arr)
            out.push_back (f);
    return out;
}
} // namespace

TEST_CASE ("goldens.json is readable", "[golden]")
{
    REQUIRE (fixtures().size() >= 1);
}

TEST_CASE ("Golden fixtures: tempo, key, downbeat", "[golden]")
{
    const auto all = fixtures();
    REQUIRE_FALSE (all.empty());
    const auto fixture = all[(size_t) GENERATE (range (0, 5))];

    const auto file = sourceDir().getChildFile (fixture["file"].toString());
    if (! file.existsAsFile())
        SKIP ("fixture missing: " << file.getFullPathName() << " (cmake --build build --target fixtures)");

    const AudioImporter importer;
    const auto imported = importer.importFile (file);
    REQUIRE (imported.ok());

    AppConfig config; // PR CI backends: goldens.json key_backends_in_pr_ci == ["chroma"]
    config.keyBackend = "chroma";
    auto keyDetector = makeKeyDetector (config);
    const auto result = analyze (*imported.capture, *keyDetector.detector);

    INFO (file.getFileName() << ": bpm " << result.rhythm.bpm << ", key " << theory::camelot (result.key.top3.at (0).key)
                             << " (" << theory::traditionalName (result.key.top3[0].key) << "), margin " << result.key.margin);

    // --- tempo
    const auto tempo = fixture["tempo"];
    if (tempo.isObject())
    {
        const double bpm = tempo["bpm"];
        const double tolerance = (double) tempo["tolerance_pct"] / 100.0;
        CHECK_THAT (result.rhythm.bpm, Catch::Matchers::WithinRel (bpm, tolerance));
    }

    // --- key
    const auto key = fixture["key"];
    const juce::String expected = key["camelot"];
    const bool allowFallback = key.getProperty ("allow_fallback", false);
    const float minMargin = (float) (double) key["min_margin"];

    if (allowFallback)
    {
        const bool inTop2 = theory::camelot (result.key.top3[0].key) == expected.toStdString()
                         || theory::camelot (result.key.top3[1].key) == expected.toStdString();
        CHECK (inTop2);
    }
    else
    {
        CHECK (theory::camelot (result.key.top3[0].key) == expected.toStdString());
        CHECK (result.key.margin >= minMargin);
    }

    // --- downbeat (synthetic fixtures carry exact ground truth)
    const auto truthFile = file.withFileExtension ("truth.json");
    if (truthFile.existsAsFile() && tempo.isObject())
    {
        const auto truth = juce::JSON::parse (truthFile);
        const double first = truth["first_downbeat_sec"];
        const double barSec = 4.0 * 60.0 / (double) truth["bpm"];
        REQUIRE_FALSE (result.rhythm.downbeats.empty());
        for (double d : result.rhythm.downbeats)
        {
            const double bars = (d - first) / barSec;
            CHECK_THAT ((bars - std::round (bars)) * barSec, Catch::Matchers::WithinAbs (0.0, 0.010));
        }
    }
}
