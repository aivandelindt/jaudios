#pragma once

#include "engine/theory/Key.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <array>
#include <map>
#include <memory>
#include <vector>

namespace jaudios::analysis
{

using theory::Key;

enum class KeyBackend { sidecar, onnx, chroma };

const char* toString (KeyBackend) noexcept;

struct KeyCandidate
{
    Key key;
    int camelotNumber = 0;
    char camelotLetter = 'A';
    float score = 0.0f;
};

struct KeyResult
{
    std::vector<KeyCandidate> top3;  // sorted desc by score
    float margin = 0.0f;             // top1.score - top2.score (softmax)
    KeyBackend backend = KeyBackend::chroma;
    bool scoresAvailable = true;     // false: backend only returns argmax (UI hides alternatives)
    bool lowConfidence = false;      // margin < lowConfidenceMargin, or no tonal content
    std::array<float, 24> scores {}; // softmax over theory::index24 order (when scoresAvailable)
};

inline constexpr float lowConfidenceMargin = 0.15f;

/** Key detection backends. Analysis thread only: never the audio or message thread. */
class IKeyDetector
{
public:
    virtual ~IKeyDetector() = default;
    virtual KeyResult analyzeFile (const juce::File&) = 0;
    virtual KeyResult analyzeBuffer (const juce::AudioBuffer<float>&, double sampleRate) = 0;
};

/** Runtime configuration. KEY_BACKEND = sidecar | onnx | chroma (default chroma). */
struct AppConfig
{
    juce::String keyBackend = "chroma";

    static AppConfig fromEnvironment();
    static AppConfig fromEnvironment (const std::map<juce::String, juce::String>& env);
};

struct KeyDetectorHandle
{
    std::unique_ptr<IKeyDetector> detector;
    KeyBackend backend = KeyBackend::chroma;
    juce::String warning; // non-empty when the requested backend was unavailable
};

/** Builds the requested backend, falling back to chroma (with a warning) when it is unavailable.
    In this build only `chroma` exists: the OpenKeyScan sidecar (Phase A) and ONNX (Phase B)
    backends are pending dependency approval. */
KeyDetectorHandle makeKeyDetector (const AppConfig&);

} // namespace jaudios::analysis
