#include "KeyDetectorOnnx.h"

#include "OpenKeyScanProtocol.h"

#include "engine/analysis/AudioImport.h"
#include "engine/analysis/Log.h"
#include "engine/analysis/Resampler.h"
#include "engine/analysis/key/KeyDetectorChroma.h"
#include "engine/dsp/CQT.h"

#include "engine/util/Sha256.h"

#if JAUDIOS_HAS_ONNXRUNTIME
 #include <onnxruntime_cxx_api.h>
 #if JUCE_MAC
// Exported by the macOS libonnxruntime (coreml_provider_factory.h is not shipped in the NuGet headers).
extern "C" OrtStatus* ORT_API_CALL OrtSessionOptionsAppendExecutionProvider_CoreML (OrtSessionOptions*, uint32_t flags);
 #endif
#endif

#include <algorithm>
#include <cmath>
#include <numeric>

namespace jaudios::analysis::openkeyscan
{

constexpr double kExportParityThreshold = 1e-4; // ml/export_openkeyscan.py PARITY

juce::String KeyDetectorOnnx::expectedModelSha256()
{
   #ifdef JAUDIOS_OPENKEYSCAN_ONNX_SHA256
    return JAUDIOS_OPENKEYSCAN_ONNX_SHA256;
   #else
    return {};
   #endif
}

juce::File provenanceFileFor (const juce::File& model)
{
    return model.getSiblingFile (model.getFileName() + ".provenance.json");
}

std::optional<juce::String> verifyLocalExport (const juce::File& model, const juce::String& actualSha, juce::String& why)
{
    const auto file = provenanceFileFor (model);
    if (! file.existsAsFile())
    {
        why = "no " + file.getFileName();
        return std::nullopt;
    }

    const auto p = juce::JSON::parse (file);
    if (! p.isObject())
    {
        why = file.getFileName() + " is not valid JSON";
        return std::nullopt;
    }

    if (p["sha256"].toString() != actualSha)
    {
        why = "provenance sha256 does not match the file (modified or truncated after export)";
        return std::nullopt;
    }

   #ifdef JAUDIOS_OPENKEYSCAN_SOURCE_COMMIT
    if (p["source_commit"].toString() != JAUDIOS_OPENKEYSCAN_SOURCE_COMMIT)
    {
        why = "exported from a different upstream commit (" + p["source_commit"].toString().substring (0, 8) + ")";
        return std::nullopt;
    }
   #endif
   #ifdef JAUDIOS_OPENKEYSCAN_CHECKPOINT_SHA256
    if (p["checkpoint_sha256"].toString() != JAUDIOS_OPENKEYSCAN_CHECKPOINT_SHA256)
    {
        why = "exported from a different checkpoint";
        return std::nullopt;
    }
   #endif

    const double parity = p.getProperty ("parity_max_abs_logits", -1.0);
    const double threshold = p.getProperty ("parity_threshold", 0.0);
    if (! (parity >= 0.0 && threshold > 0.0 && threshold <= kExportParityThreshold && parity < threshold)
        || ! (bool) p.getProperty ("argmax_match", false))
    {
        why = "provenance does not record a passing parity check";
        return std::nullopt;
    }

    return p["platform"].toString();
}

KeyResult keyResultFromLogits (const std::array<float, 24>& logits)
{
    KeyResult r;
    r.backend = KeyBackend::onnx;
    r.scoresAvailable = true;

    const float maxLogit = *std::max_element (logits.begin(), logits.end());
    std::array<double, 24> p {};
    double sum = 0.0;
    for (size_t c = 0; c < 24; ++c)
        sum += (p[c] = std::exp ((double) logits[c] - maxLogit));

    std::array<int, 24> order {};
    std::iota (order.begin(), order.end(), 0);
    std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return p[(size_t) a] > p[(size_t) b]; });

    for (int c = 0; c < 24; ++c)
    {
        const auto key = *keyFromClassIndex (c);
        r.scores[(size_t) theory::index24 (key)] = (float) (p[(size_t) c] / sum);
    }
    for (int i = 0; i < 3; ++i)
    {
        const auto key = *keyFromClassIndex (order[(size_t) i]);
        r.top3.push_back ({ key, theory::camelotNumber (key), theory::camelotLetter (key), (float) (p[(size_t) order[(size_t) i]] / sum) });
    }
    r.margin = r.top3[0].score - r.top3[1].score;
    r.lowConfidence = r.margin < lowConfidenceMargin;
    return r;
}

//==============================================================================
struct KeyDetectorOnnx::Impl
{
    dsp::ConstantQ cqt { dsp::ConstantQ::openKeyScan() };
   #if JAUDIOS_HAS_ONNXRUNTIME
    std::unique_ptr<Ort::Session> session;

    static Ort::Env& env()
    {
        static Ort::Env e (ORT_LOGGING_LEVEL_WARNING, "JAudios");
        return e;
    }
   #endif
};

KeyDetectorOnnx::KeyDetectorOnnx (OnnxConfig c) : config (std::move (c))
{
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    impl = std::make_unique<Impl>();
    timings.cqtBasisMs = juce::Time::getMillisecondCounterHiRes() - t0;
    log::debug ("ONNX key detector: CQT basis " + juce::String (timings.cqtBasisMs, 1) + " ms");
   #if ! JAUDIOS_HAS_ONNXRUNTIME
    error = "built without ONNX Runtime (JAUDIOS_WITH_ONNXRUNTIME=OFF)";
   #else
    if (! config.modelFile.existsAsFile())
    {
        error = "key model not found: " + config.modelFile.getFullPathName();
        return;
    }

    const auto expected = expectedModelSha256();
    t0 = juce::Time::getMillisecondCounterHiRes();
    if (expected.isNotEmpty())
    {
        const juce::String got (util::Sha256::ofFile (config.modelFile.getFullPathName().toStdString()));
        if (got == expected)
            origin = "canonical";
        else
        {
            // Not the manifest's file: accept a model exported and verified by
            // ml/export_openkeyscan.py on this machine (e.g. on macOS arm64, where the export is not
            // byte-identical to the Linux one), proven by its provenance record.
            juce::String why;
            if (const auto platform = verifyLocalExport (config.modelFile, got, why))
            {
                origin = "local export (" + *platform + ")";
                log::info ("Key model " + config.modelFile.getFileName() + " is a verified local export from "
                           + *platform + " (sha256 " + got.substring (0, 12) + "...)");
            }
            else
            {
                error = "key model sha256 mismatch (" + got.substring (0, 12) + "... != " + expected.substring (0, 12)
                      + "...) and not a verified local export: " + why;
                return;
            }
        }
    }

    timings.sha256Ms = juce::Time::getMillisecondCounterHiRes() - t0;
    log::debug ("ONNX key detector: sha256 " + juce::String (timings.sha256Ms, 1) + " ms");
    t0 = juce::Time::getMillisecondCounterHiRes();

    try
    {
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
        provider = "CPU";

       #if JUCE_MAC
        if (config.preferCoreML)
        {
            if (auto* status = OrtSessionOptionsAppendExecutionProvider_CoreML (options, 0); status == nullptr)
                provider = "CoreML";
            else
            {
                log::warning (juce::String ("CoreML execution provider unavailable: ") + Ort::GetApi().GetErrorMessage (status));
                Ort::GetApi().ReleaseStatus (status);
            }
        }
       #endif

       #if JUCE_WINDOWS
        impl->session = std::make_unique<Ort::Session> (Impl::env(), config.modelFile.getFullPathName().toWideCharPointer(), options);
       #else
        impl->session = std::make_unique<Ort::Session> (Impl::env(), config.modelFile.getFullPathName().toRawUTF8(), options);
       #endif
    }
    catch (const Ort::Exception& e)
    {
        error = juce::String ("ONNX Runtime: ") + e.what();
        impl->session.reset();
    }
    timings.sessionMs = juce::Time::getMillisecondCounterHiRes() - t0;
    log::debug ("ONNX key detector: session (" + provider + ") " + juce::String (timings.sessionMs, 1) + " ms");
   #endif

    if (error.isNotEmpty())
        log::warning ("ONNX key detector unavailable: " + error);
}

KeyDetectorOnnx::~KeyDetectorOnnx() = default;

bool KeyDetectorOnnx::isReady() const noexcept
{
   #if JAUDIOS_HAS_ONNXRUNTIME
    return impl->session != nullptr;
   #else
    return false;
   #endif
}

std::optional<std::array<float, 24>> KeyDetectorOnnx::logitsFor (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
   #if JAUDIOS_HAS_ONNXRUNTIME
    if (! isReady())
        return std::nullopt;

    // Same audio path as upstream for WAV input: mono mean of channels at 44.1 kHz.
    juce::AudioBuffer<float> mono (1, buffer.getNumSamples());
    mono.clear();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        mono.addFrom (0, 0, buffer, ch, 0, buffer.getNumSamples(), 1.0f / (float) std::max (1, buffer.getNumChannels()));
    if (sampleRate != analysisSampleRate)
        mono = resample (mono, sampleRate, analysisSampleRate);

    auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto input = dsp::openKeyScanModelInput (impl->cqt, mono.getReadPointer (0), mono.getNumSamples());
    log::debug ("ONNX key detector: CQT " + juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 1) + " ms for "
                + juce::String (input.numFrames) + " frames");
    if (input.numFrames < 1)
        return std::nullopt;
    t0 = juce::Time::getMillisecondCounterHiRes();

    try
    {
        const std::array<int64_t, 4> shape { 1, 1, input.numBins, input.numFrames };
        auto memory = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        auto tensor = Ort::Value::CreateTensor<float> (memory, input.values.data(), input.values.size(), shape.data(), shape.size());

        const char* inNames[] = { "cqt" };
        const char* outNames[] = { "logits" };
        auto outputs = impl->session->Run (Ort::RunOptions { nullptr }, inNames, &tensor, 1, outNames, 1);

        log::debug ("ONNX key detector: inference " + juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 1) + " ms");
        const float* out = outputs[0].GetTensorData<float>();
        std::array<float, 24> logits {};
        std::copy (out, out + 24, logits.begin());
        return logits;
    }
    catch (const Ort::Exception& e)
    {
        error = juce::String ("ONNX Runtime: ") + e.what();
        return std::nullopt;
    }
   #else
    juce::ignoreUnused (buffer, sampleRate);
    return std::nullopt;
   #endif
}

KeyResult KeyDetectorOnnx::chromaFallback (const juce::AudioBuffer<float>& buffer, double sampleRate, const juce::String& warning) const
{
    KeyDetectorChroma chroma;
    auto r = chroma.analyzeBuffer (buffer, sampleRate);
    r.warning = warning;
    return r;
}

KeyResult KeyDetectorOnnx::analyzeBuffer (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    if (! isReady())
        return chromaFallback (buffer, sampleRate, "Key model unavailable (" + error + "); using chroma");

    const auto logits = logitsFor (buffer, sampleRate);
    if (! logits)
        return chromaFallback (buffer, sampleRate,
                               error.isNotEmpty() ? "Key model failed (" + error + "); used chroma"
                                                  : juce::String ("Audio too short for the key model; used chroma"));

    auto result = keyResultFromLogits (*logits);

    // Ensemble for short inputs (skill): run chroma too; on disagreement keep the model unless
    // its margin < 0.15, then chroma's key becomes the runner-up and the result is low-confidence.
    if (buffer.getNumSamples() < config.ensembleBelowSec * sampleRate)
    {
        auto chroma = std::make_shared<KeyResult> (KeyDetectorChroma().analyzeBuffer (buffer, sampleRate));
        const bool agree = ! chroma->top3.empty() && chroma->top3[0].key == result.top3[0].key;
        result.chromaAgrees = agree;
        result.ensembleChroma = chroma;
        if (! agree && result.margin < lowConfidenceMargin && ! chroma->top3.empty())
        {
            result.top3.resize (1);
            result.top3.push_back (chroma->top3[0]);
            if (! (chroma->top3[0].key == result.top3[0].key) && chroma->top3.size() > 1)
                result.top3.push_back (chroma->top3[1]);
            result.lowConfidence = true;
        }
    }
    return result;
}

KeyResult KeyDetectorOnnx::analyzeFile (const juce::File& file)
{
    const AudioImporter importer;
    const auto imported = importer.importFile (file);
    if (! imported.ok())
    {
        auto r = KeyDetectorChroma::scoreChroma ({});
        r.warning = imported.error;
        return r;
    }
    return analyzeBuffer (imported.capture->buffer, imported.capture->sampleRate);
}

} // namespace jaudios::analysis::openkeyscan
