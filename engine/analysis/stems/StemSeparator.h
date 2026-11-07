#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace jaudios::analysis::stems
{

/** Model output order (htdemucs `model.sources`). */
enum class Stem : int { drums = 0, bass = 1, other = 2, vocals = 3 };
constexpr int numStems = 4;
constexpr std::array<Stem, numStems> allStems { Stem::drums, Stem::bass, Stem::other, Stem::vocals };
const char* stemName (Stem) noexcept;              // "drums", "bass", "other", "vocals"
std::optional<Stem> stemFromName (const juce::String&);

/** Four stereo stems, each the length of the input and sample-aligned with it, at the input's rate. */
struct Stems
{
    double sampleRate = 0.0;
    std::array<juce::AudioBuffer<float>, numStems> buffers;

    juce::AudioBuffer<float>& operator[] (Stem s) noexcept { return buffers[(size_t) s]; }
    const juce::AudioBuffer<float>& operator[] (Stem s) const noexcept { return buffers[(size_t) s]; }
    int getNumSamples() const noexcept { return buffers[0].getNumSamples(); }
};

/** 8-channel buffer for AudioEngine::setStems: stem s (model order), channel c -> channel 2s+c. */
juce::AudioBuffer<float> packStems (const Stems&);

/** Which stems feed the harmonic analysis ("guide"; stem-separation skill: default other + vocals). */
struct GuideSelection
{
    std::array<bool, numStems> use { false, false, true, true }; // drums, bass, other, vocals

    bool includes (Stem s) const noexcept { return use[(size_t) s]; }
    bool empty() const noexcept { return ! (use[0] || use[1] || use[2] || use[3]); }
    juce::String toString() const;                                   // "other+vocals"
    static std::optional<GuideSelection> fromString (const juce::String&); // "other+vocals", "bass", "all"
};

/** Sum of the selected stems (stereo, stems' length and rate). Empty selection -> silence. */
juce::AudioBuffer<float> mixGuide (const Stems&, const GuideSelection& = {});

struct SeparatorConfig
{
    juce::File modelFile;          // htdemucs.onnx (sha256 vs models/manifest.json, or verified provenance)
    // CoreML execution provider: OFF until the [stems][coreml] golden test passes on Apple Silicon
    // (with ONNX Runtime 1.19.2 it gave wrong results for the key model). JAUDIOS_ONNX_COREML=1 to try.
    bool preferCoreML = false;
    int intraOpThreads = 0;        // 0 = ONNX Runtime default (all physical cores)
    double overlap = 0.25;         // demucs.apply.apply_model default
};

/** `<model>.provenance.json` check for htdemucs.onnx (written by ml/export_demucs.py): matches the
    file's sha256, the pinned demucs version + checkpoint, and records a passing parity (< 1e-3).
    Returns the export platform, or nullopt with the reason in `why`. */
std::optional<juce::String> verifyDemucsExport (const juce::File& model, const juce::String& actualSha, juce::String& why);

/** 4-stem separation with HT-Demucs through ONNX Runtime (stem-separation skill).
    Same result as demucs.api.Separator(shifts=0): the input is normalised by its mono reference,
    split like demucs.apply.apply_model (7.8 s segments, 25 % overlap, triangular cross-fade), each
    segment runs DemucsSpec (STFT) -> ONNX core -> DemucsSpec (ISTFT) + time branch, then
    de-normalised. Non-44.1 kHz input is resampled to 44.1 kHz and the stems back.
    Worker thread only (never the audio thread): the constructor loads a 174 MB model; separate()
    allocates and takes ~2 s per segment on CPU. One separate() at a time per instance. */
class StemSeparator
{
public:
    explicit StemSeparator (SeparatorConfig);
    ~StemSeparator();

    bool isReady() const noexcept;
    const juce::String& loadError() const noexcept { return error; }
    const juce::String& executionProvider() const noexcept { return provider; }
    const juce::String& modelOrigin() const noexcept { return origin; } // "canonical" / "local export (<platform>)"
    /** Identifies what separate() computes (model sha256 + settings): part of the stem cache key. */
    juce::String cacheId() const;

    struct Result
    {
        std::optional<Stems> stems;     // empty on error or cancel
        juce::String error;
        bool cancelled = false;
        int segments = 0;
        double seconds = 0.0;           // wall time of separate()
    };

    /** progress: called on the calling thread after each segment with the done fraction (0..1]. */
    Result separate (const juce::AudioBuffer<float>& input, double sampleRate,
                     const std::function<void (double)>& progress = {});

    /** Thread-safe: cancels the separate() in progress (stops inside the running inference too).
        A separate() started afterwards runs normally. */
    void cancel() noexcept;

    /** Number of model segments separate() runs for this many 44.1 kHz samples. */
    static int segmentCount (int64_t samplesAt44k, double overlap = 0.25) noexcept;

    /** Expected sha256 of htdemucs.onnx (models/manifest.json at configure time). */
    static juce::String expectedModelSha256();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    SeparatorConfig config;
    juce::String error, provider, origin, modelSha;
    std::atomic<bool> cancelRequested { false };
};

} // namespace jaudios::analysis::stems
