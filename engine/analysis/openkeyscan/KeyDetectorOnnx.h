#pragma once

#include "engine/analysis/key/KeyDetector.h"

#include <array>
#include <memory>
#include <optional>

namespace jaudios::analysis::openkeyscan
{

struct OnnxConfig
{
    juce::File modelFile;              // openkeyscan3.onnx (sha256 checked against models/manifest.json)
    bool preferCoreML = true;          // macOS: CoreML EP first, CPU fallback
    double ensembleBelowSec = 30.0;    // shorter inputs also run chroma (skill ensemble rule)
};

/** `<model>.provenance.json`, written by ml/export_openkeyscan.py next to the model it exported. */
juce::File provenanceFileFor (const juce::File& model);

/** A model whose sha256 differs from the manifest is accepted only if its provenance record matches
    the file's sha256, comes from the pinned upstream commit + checkpoint, and records a passing
    PyTorch-vs-ORT parity check. Returns the export platform, or nullopt with the reason in `why`. */
std::optional<juce::String> verifyLocalExport (const juce::File& model, const juce::String& actualSha, juce::String& why);

/** Softmax over the 24 logits (upstream class order) -> KeyResult (top3, margin, scores). */
KeyResult keyResultFromLogits (const std::array<float, 24>& logits);

/** Phase B key backend: OpenKeyScan CNN in-process via ONNX Runtime.
    Input: our C++ port of the upstream CQT (engine/dsp/CQT.h) on mono 44.1 kHz audio; no temp
    files. The session is created in the constructor (call it off the audio/message thread).
    If the model is missing / mismatched or the session fails, every call returns the chroma result
    with a warning. */
class KeyDetectorOnnx final : public IKeyDetector
{
public:
    explicit KeyDetectorOnnx (OnnxConfig);
    ~KeyDetectorOnnx() override;

    KeyResult analyzeFile (const juce::File&) override;
    KeyResult analyzeBuffer (const juce::AudioBuffer<float>&, double sampleRate) override;

    /** Raw logits for the buffer, or nullopt (not ready / too short). For tests and diagnostics. */
    std::optional<std::array<float, 24>> logitsFor (const juce::AudioBuffer<float>&, double sampleRate);

    bool isReady() const noexcept;
    const juce::String& loadError() const noexcept { return error; }
    const juce::String& executionProvider() const noexcept { return provider; }
    /** "canonical" (manifest sha256) or "local export (<platform>)". */
    const juce::String& modelOrigin() const noexcept { return origin; }

    struct LoadTimings { double cqtBasisMs = 0, sha256Ms = 0, sessionMs = 0; };
    const LoadTimings& loadTimings() const noexcept { return timings; }

    /** Expected sha256 of the model (from models/manifest.json at configure time). */
    static juce::String expectedModelSha256();

private:
    KeyResult chromaFallback (const juce::AudioBuffer<float>&, double sampleRate, const juce::String& warning) const;

    struct Impl;
    std::unique_ptr<Impl> impl;
    OnnxConfig config;
    juce::String error, provider, origin;
    LoadTimings timings;
};

} // namespace jaudios::analysis::openkeyscan
