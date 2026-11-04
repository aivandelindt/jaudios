#include "TestSignals.h"

#include "engine/analysis/key/KeyDetector.h"
#include "engine/analysis/openkeyscan/KeyDetectorOnnx.h"
#include "engine/analysis/openkeyscan/OpenKeyScanProtocol.h"
#include "engine/theory/Key.h"
#include "engine/util/Sha256.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace jaudios;
using namespace jaudios::analysis;
using namespace jaudios::analysis::openkeyscan;
using Catch::Matchers::WithinAbs;

namespace
{
juce::File devModel() { return juce::File (JAUDIOS_DEV_MODELS_DIR).getChildFile ("openkeyscan3.onnx"); }
#define REQUIRE_MODEL() if (! devModel().existsAsFile()) SKIP ("no model: python3 tools/models.py --only onnx --python <env>")
} // namespace

TEST_CASE ("Logits map to a KeyResult in upstream class order", "[onnx]")
{
    std::array<float, 24> logits {};
    logits.fill (0.0f);
    logits[7] = 5.0f;  // class 7 = 8A, A minor
    logits[19] = 4.0f; // class 19 = 8B, C major

    const auto r = keyResultFromLogits (logits);
    REQUIRE (r.top3.size() == 3);
    CHECK (r.backend == KeyBackend::onnx);
    CHECK (r.scoresAvailable);
    CHECK (r.top3[0].key == theory::Key { 9, theory::Mode::minor });
    CHECK (r.top3[1].key == theory::Key { 0, theory::Mode::major });

    // softmax: p7 = e^5 / (e^5 + e^4 + 22)
    const double z = std::exp (5.0) + std::exp (4.0) + 22.0;
    CHECK_THAT (r.top3[0].score, WithinAbs (std::exp (5.0) / z, 1e-6));
    CHECK_THAT (r.margin, WithinAbs ((std::exp (5.0) - std::exp (4.0)) / z, 1e-6));
    CHECK_FALSE (r.lowConfidence);

    float sum = 0.0f;
    for (float s : r.scores)
        sum += s;
    CHECK_THAT (sum, WithinAbs (1.0, 1e-5));
    CHECK_THAT (r.scores[(size_t) theory::index24 (theory::Key { 9, theory::Mode::minor })], WithinAbs (r.top3[0].score, 1e-7));
}

TEST_CASE ("ONNX detector loads the model and is fast to create", "[onnx]")
{
    REQUIRE_MODEL();
    OnnxConfig config;
    config.modelFile = devModel();

    // Warm-up: the first construction in a process also pays one-off costs (loading the ONNX
    // Runtime dylib, a cold disk cache for the 18.8 MB model on a fresh CI runner). Time the second.
    { KeyDetectorOnnx warmUp (config); }

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    KeyDetectorOnnx detector (config);
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;

    REQUIRE (detector.isReady());
    CHECK (detector.loadError().isEmpty());
    const auto& t = detector.loadTimings();
    INFO ("provider " << detector.executionProvider() << ": session " << t.sessionMs << " ms, sha256 " << t.sha256Ms
                      << " ms, CQT basis " << t.cqtBasisMs << " ms, total " << ms << " ms");
   #if JUCE_DEBUG
    WARN ("timing not asserted in Debug/sanitizer builds"); // unoptimised: numbers are meaningless
   #else
    CHECK (t.sessionMs < 300.0); // skill acceptance: < 300 ms session creation
    CHECK (ms < 1000.0);         // whole detector incl. model verification and CQT basis
   #endif
}

TEST_CASE ("ONNX detector refuses a model whose sha256 does not match the manifest", "[onnx]")
{
    test::TempDir tmp;
    const auto bogus = tmp.dir.getChildFile ("openkeyscan3.onnx");
    bogus.replaceWithText ("not a model");

    OnnxConfig config;
    config.modelFile = bogus;
    KeyDetectorOnnx detector (config);
    CHECK_FALSE (detector.isReady());
    CHECK (detector.loadError().contains ("sha256"));

    // analysis still returns a (chroma) result with a warning
    auto audio = test::renderNotes (test::progression (7, false, { 0, 3, 4, 0 }, 1.0, 10), 40.5);
    const auto r = detector.analyzeBuffer (audio, 44100.0);
    CHECK (r.backend == KeyBackend::chroma);
    CHECK (r.warning.isNotEmpty());
}

TEST_CASE ("ONNX detector analyses audio with real scores", "[onnx]")
{
    REQUIRE_MODEL();
    OnnxConfig config;
    config.modelFile = devModel();
    KeyDetectorOnnx detector (config);
    REQUIRE (detector.isReady());

    auto audio = test::renderNotes (test::progression (7, false, { 0, 3, 4, 0 }, 1.0, 10), 40.5); // 40 s, no ensemble
    const auto r = detector.analyzeBuffer (audio, 44100.0);

    CHECK (r.backend == KeyBackend::onnx);
    CHECK (r.scoresAvailable);
    REQUIRE (r.top3.size() == 3);
    CHECK (r.top3[0].score >= r.top3[1].score);
    CHECK_THAT (r.margin, WithinAbs (r.top3[0].score - r.top3[1].score, 1e-6));
    CHECK_FALSE (r.ensembleChroma);
}

TEST_CASE ("ONNX detector: clips under 30 s also run chroma; too-short clips fall back", "[onnx][ensemble]")
{
    REQUIRE_MODEL();
    OnnxConfig config;
    config.modelFile = devModel();
    KeyDetectorOnnx detector (config);
    REQUIRE (detector.isReady());

    SECTION ("12 s clip: ensemble")
    {
        auto audio = test::renderNotes (test::progression (7, false, { 0, 3, 4, 0 }, 1.0, 3), 12.5);
        const auto r = detector.analyzeBuffer (audio, 44100.0);
        CHECK (r.backend == KeyBackend::onnx);
        REQUIRE (r.ensembleChroma != nullptr);
        REQUIRE (r.chromaAgrees.has_value());
        if (! *r.chromaAgrees && r.margin < lowConfidenceMargin)
        {
            CHECK (r.lowConfidence);
            CHECK (r.top3[1].key == r.ensembleChroma->top3[0].key);
        }
    }
    SECTION ("0.3 s clip: no CQT frames -> chroma with a warning")
    {
        auto audio = test::makeSine (1, 44100 * 3 / 10, 440.0, 44100.0);
        const auto r = detector.analyzeBuffer (*audio, 44100.0);
        CHECK (r.backend == KeyBackend::chroma);
        CHECK (r.warning.isNotEmpty());
    }
}

TEST_CASE ("makeKeyDetector: onnx backend and auto preference", "[onnx][factory]")
{
    SECTION ("onnx with the dev model")
    {
        REQUIRE_MODEL();
        AppConfig config;
        config.keyBackend = "onnx";
        config.modelsDir = devModel().getParentDirectory();
        auto d = makeKeyDetector (config);
        CHECK (d.backend == KeyBackend::onnx);
        CHECK (d.warning.isEmpty());
    }
    SECTION ("auto prefers onnx when a model is present")
    {
        REQUIRE_MODEL();
        AppConfig config;
        config.keyBackend = "auto";
        config.modelsDir = devModel().getParentDirectory();
        config.sidecarExecutable = juce::File ("/nonexistent/openkeyscan-analyzer");
        CHECK (makeKeyDetector (config).backend == KeyBackend::onnx);
    }
    SECTION ("onnx without a model falls back to chroma with a warning")
    {
        test::TempDir tmp;
        AppConfig config;
        config.keyBackend = "onnx";
        config.modelsDir = tmp.dir;
        auto d = makeKeyDetector (config);
        CHECK (d.backend == KeyBackend::chroma);
        CHECK (d.warning.contains ("model"));
    }
    SECTION ("default is onnx; without a model it is chroma with a warning")
    {
        REQUIRE_MODEL();
        AppConfig withModel;
        withModel.modelsDir = devModel().getParentDirectory();
        CHECK (makeKeyDetector (withModel).backend == KeyBackend::onnx);

        test::TempDir tmp;
        AppConfig noModel;
        noModel.modelsDir = tmp.dir;
        const auto d = makeKeyDetector (noModel);
        CHECK (d.backend == KeyBackend::chroma);
        CHECK (d.warning.contains ("make models"));

        AppConfig empty;
        empty.keyBackend = "";
        empty.modelsDir = withModel.modelsDir;
        CHECK (makeKeyDetector (empty).backend == KeyBackend::onnx);
        CHECK (AppConfig::fromEnvironment ({ { "JAUDIOS_MODELS_DIR", "/opt/m" } }).modelsDir.getFullPathName() == "/opt/m");
    }
}

namespace
{
/** Valid ONNX model whose bytes differ from the canonical one: an extra unknown protobuf field
    (field 1000, varint 1) appended to ModelProto, which ONNX Runtime's parser keeps and ignores.
    Stands in for an export whose bytes differ by platform (macOS arm64). */
juce::File writeVariantModel (const juce::File& dir)
{
    juce::MemoryBlock mb;
    devModel().loadFileAsData (mb);
    const unsigned char unknownField[] = { 0xC0, 0x3E, 0x01 }; // key (1000 << 3 | 0) = 8000, value 1
    mb.append (unknownField, sizeof (unknownField));
    const auto f = dir.getChildFile ("openkeyscan3.onnx");
    f.replaceWithData (mb.getData(), mb.getSize());
    return f;
}

juce::var manifest() { return juce::JSON::parse (juce::File (JAUDIOS_SOURCE_DIR).getChildFile ("models/manifest.json")); }

void writeProvenance (const juce::File& model, std::function<void (juce::DynamicObject&)> tweak = {})
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("sha256", juce::String (jaudios::util::Sha256::ofFile (model.getFullPathName().toStdString())));
    o->setProperty ("source_commit", manifest()["openkeyscan_source"]["commit"]);
    o->setProperty ("checkpoint_sha256", manifest()["openkeyscan3_pt"]["sha256"]);
    o->setProperty ("parity_max_abs_logits", 1.4e-6);
    o->setProperty ("parity_threshold", 1e-4);
    o->setProperty ("argmax_match", true);
    o->setProperty ("platform", "darwin-arm64");
    if (tweak)
        tweak (*o);
    provenanceFileFor (model).replaceWithText (juce::JSON::toString (juce::var (o)));
}
} // namespace

TEST_CASE ("A verified local export with a different sha256 is accepted", "[onnx][provenance]")
{
    REQUIRE_MODEL();
    test::TempDir tmp;
    const auto model = writeVariantModel (tmp.dir);
    REQUIRE (juce::String (jaudios::util::Sha256::ofFile (model.getFullPathName().toStdString())) != KeyDetectorOnnx::expectedModelSha256());
    writeProvenance (model);

    OnnxConfig config;
    config.modelFile = model;
    KeyDetectorOnnx detector (config);

    REQUIRE (detector.isReady());
    CHECK (detector.modelOrigin() == "local export (darwin-arm64)");

    auto audio = test::renderNotes (test::progression (7, false, { 0, 3, 4, 0 }, 1.0, 10), 40.5);
    CHECK (detector.analyzeBuffer (audio, 44100.0).backend == KeyBackend::onnx);

    // The dev model is reported as "canonical" only when it is the manifest's exact file. Exports are
    // not byte-reproducible across machines (onnxsim constant folding runs on CPU-specific ORT
    // kernels), so on CI it is usually a verified local export of its own.
    OnnxConfig dev;
    dev.modelFile = devModel();
    const bool isManifestFile = juce::String (jaudios::util::Sha256::ofFile (devModel().getFullPathName().toStdString()))
                                == KeyDetectorOnnx::expectedModelSha256();
    const auto devOrigin = KeyDetectorOnnx (dev).modelOrigin();
    INFO ("dev model origin: " << devOrigin);
    CHECK ((isManifestFile ? devOrigin == "canonical" : devOrigin.startsWith ("local export (")));
}

TEST_CASE ("A different sha256 without a valid provenance record is refused", "[onnx][provenance]")
{
    REQUIRE_MODEL();
    test::TempDir tmp;
    const auto model = writeVariantModel (tmp.dir);

    auto refused = [&] (const char* expectedReason)
    {
        OnnxConfig config;
        config.modelFile = model;
        KeyDetectorOnnx detector (config);
        INFO (detector.loadError());
        CHECK_FALSE (detector.isReady());
        CHECK (detector.loadError().contains (expectedReason));
    };

    SECTION ("no record (e.g. a crashed export)") { refused ("no openkeyscan3.onnx.provenance.json"); }
    SECTION ("record for another file")
    {
        writeProvenance (model, [] (juce::DynamicObject& o) { o.setProperty ("sha256", juce::String::repeatedString ("0", 64)); });
        refused ("does not match the file");
    }
    SECTION ("failed parity")
    {
        writeProvenance (model, [] (juce::DynamicObject& o) { o.setProperty ("parity_max_abs_logits", 3e-3); });
        refused ("passing parity");
    }
    SECTION ("loosened threshold")
    {
        writeProvenance (model, [] (juce::DynamicObject& o) { o.setProperty ("parity_threshold", 1.0); o.setProperty ("parity_max_abs_logits", 0.5); });
        refused ("passing parity");
    }
    SECTION ("other upstream commit")
    {
        writeProvenance (model, [] (juce::DynamicObject& o) { o.setProperty ("source_commit", "deadbeef"); });
        refused ("different upstream commit");
    }
    SECTION ("other checkpoint")
    {
        writeProvenance (model, [] (juce::DynamicObject& o) { o.setProperty ("checkpoint_sha256", "x"); });
        refused ("different checkpoint");
    }
}
