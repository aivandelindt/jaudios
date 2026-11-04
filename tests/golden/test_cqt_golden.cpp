// C++ CQT vs librosa via upstream preprocess_audio() (ml/tests/cqt_reference.py output).
#include "NpyReader.h"

#include "engine/analysis/AudioImport.h"
#include "engine/dsp/CQT.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

using namespace jaudios;
using namespace jaudios::dsp;

TEST_CASE ("C++ CQT matches librosa (upstream preprocess_audio) on the fixtures", "[cqt][golden]")
{
    const char* names[] = { "house_124_Amin", "techno_132_Fmin", "pop_100_Cmaj", "loop_8bar_Gmaj", "stem_bass_Dmin" };
    const auto name = juce::String (names[GENERATE (0, 1, 2, 3, 4)]);

    const auto refFile = juce::File (JAUDIOS_CQT_REFERENCE_DIR).getChildFile (name + ".cqt.npy");
    const auto wav = juce::File (JAUDIOS_SOURCE_DIR).getChildFile ("tests/fixtures/" + name + ".wav");
    if (! refFile.existsAsFile() || ! wav.existsAsFile())
        SKIP ("reference missing: configure with -DJAUDIOS_ML_PYTHON=<python3.12 env> (ml/tests/cqt_reference.py)");

    const auto ref = test::readNpy (refFile);
    REQUIRE (ref.has_value());
    REQUIRE (ref->shape.size() == 2);

    const analysis::AudioImporter importer;
    const auto imported = importer.importFile (wav);
    REQUIRE (imported.ok());
    const auto& buf = imported.capture->buffer;
    juce::AudioBuffer<float> mono (1, buf.getNumSamples());
    mono.clear();
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        mono.addFrom (0, 0, buf, ch, 0, buf.getNumSamples(), 1.0f / (float) buf.getNumChannels());

    const ConstantQ cqt (ConstantQ::openKeyScan());
    const auto input = openKeyScanModelInput (cqt, mono.getReadPointer (0), mono.getNumSamples());

    REQUIRE (ref->shape[0] == 105);
    REQUIRE ((size_t) input.numFrames == ref->shape[1]);

    float maxDiff = 0.0f, maxRef = 0.0f;
    for (int b = 0; b < 105; ++b)
        for (int t = 0; t < input.numFrames; ++t)
        {
            const float r = ref->data[(size_t) b * ref->shape[1] + (size_t) t];
            maxDiff = std::max (maxDiff, std::abs (input.at (b, t) - r));
            maxRef = std::max (maxRef, r);
        }

    INFO (name << ": max |C++ - librosa| = " << maxDiff << " (max value " << maxRef << ")");
    CHECK (maxDiff < 1e-3f); // skill acceptance: max abs diff < 1e-3 after the model's own scaling
}

#include "engine/analysis/openkeyscan/KeyDetectorOnnx.h"

TEST_CASE ("Phase B pipeline (C++ CQT + ORT) matches PyTorch on librosa input", "[onnx][golden]")
{
    const char* names[] = { "house_124_Amin", "techno_132_Fmin", "pop_100_Cmaj", "loop_8bar_Gmaj", "stem_bass_Dmin" };
    const auto name = juce::String (names[GENERATE (0, 1, 2, 3, 4)]);

    const auto refFile = juce::File (JAUDIOS_CQT_REFERENCE_DIR).getChildFile (name + ".logits.npy");
    const auto wav = juce::File (JAUDIOS_SOURCE_DIR).getChildFile ("tests/fixtures/" + name + ".wav");
    const auto model = juce::File (JAUDIOS_DEV_MODELS_DIR).getChildFile ("openkeyscan3.onnx");
    if (! refFile.existsAsFile() || ! wav.existsAsFile() || ! model.existsAsFile())
        SKIP ("needs JAUDIOS_ML_PYTHON references and build/models/openkeyscan3.onnx");

    const auto ref = test::readNpy (refFile);
    REQUIRE (ref.has_value());
    REQUIRE (ref->data.size() == 24);

    const analysis::AudioImporter importer;
    const auto imported = importer.importFile (wav);
    REQUIRE (imported.ok());

    analysis::openkeyscan::OnnxConfig config;
    config.modelFile = model;
    analysis::openkeyscan::KeyDetectorOnnx detector (config);
    REQUIRE (detector.isReady());

    const auto logits = detector.logitsFor (imported.capture->buffer, imported.capture->sampleRate);
    REQUIRE (logits.has_value());

    float maxDiff = 0.0f;
    int argRef = 0, argGot = 0;
    for (int i = 0; i < 24; ++i)
    {
        maxDiff = std::max (maxDiff, std::abs ((*logits)[(size_t) i] - ref->data[(size_t) i]));
        if (ref->data[(size_t) i] > ref->data[(size_t) argRef]) argRef = i;
        if ((*logits)[(size_t) i] > (*logits)[(size_t) argGot]) argGot = i;
    }

    INFO (name << ": class PyTorch " << argRef << " / Phase B " << argGot << ", max |dlogits| = " << maxDiff);
    CHECK (argGot == argRef); // Phase B argmax == Phase A (same weights, same server preprocessing)
    CHECK (maxDiff < 1e-2f);  // CQT drift (<1e-5) propagated through the network
}

// CoreML execution provider (macOS only). [!mayfail]: with ONNX Runtime 1.19.2 CoreML accepts 14
// of 23 nodes and returns wrong logits (off by 3-5, different key on 2 of 5 fixtures), which is
// why KeyDetectorOnnx uses the CPU provider by default. Kept so an ORT upgrade that fixes it shows.
TEST_CASE ("Phase B pipeline with the CoreML execution provider matches PyTorch", "[onnx][coreml][golden][!mayfail]")
{
   #if ! JUCE_MAC
    SKIP ("CoreML is macOS only");
   #else
    const char* names[] = { "house_124_Amin", "techno_132_Fmin", "pop_100_Cmaj", "loop_8bar_Gmaj", "stem_bass_Dmin" };
    const auto name = juce::String (names[GENERATE (0, 1, 2, 3, 4)]);

    const auto refFile = juce::File (JAUDIOS_CQT_REFERENCE_DIR).getChildFile (name + ".logits.npy");
    const auto wav = juce::File (JAUDIOS_SOURCE_DIR).getChildFile ("tests/fixtures/" + name + ".wav");
    const auto model = juce::File (JAUDIOS_DEV_MODELS_DIR).getChildFile ("openkeyscan3.onnx");
    if (! refFile.existsAsFile() || ! wav.existsAsFile() || ! model.existsAsFile())
        SKIP ("needs JAUDIOS_ML_PYTHON references and build/models/openkeyscan3.onnx");

    const auto ref = test::readNpy (refFile);
    REQUIRE (ref.has_value());
    const analysis::AudioImporter importer;
    const auto imported = importer.importFile (wav);
    REQUIRE (imported.ok());

    analysis::openkeyscan::OnnxConfig config;
    config.modelFile = model;
    config.preferCoreML = true;
    analysis::openkeyscan::KeyDetectorOnnx detector (config);
    REQUIRE (detector.isReady());
    REQUIRE (detector.executionProvider() == "CoreML");

    const auto logits = detector.logitsFor (imported.capture->buffer, imported.capture->sampleRate);
    REQUIRE (logits.has_value());
    float maxDiff = 0.0f;
    int argRef = 0, argGot = 0;
    for (int i = 0; i < 24; ++i)
    {
        maxDiff = std::max (maxDiff, std::abs ((*logits)[(size_t) i] - ref->data[(size_t) i]));
        if (ref->data[(size_t) i] > ref->data[(size_t) argRef]) argRef = i;
        if ((*logits)[(size_t) i] > (*logits)[(size_t) argGot]) argGot = i;
    }
    INFO (name << " [CoreML]: class PyTorch " << argRef << " / CoreML " << argGot << ", max |dlogits| = " << maxDiff);
    CHECK (argGot == argRef);
    CHECK (maxDiff < 1e-2f);
   #endif
}
