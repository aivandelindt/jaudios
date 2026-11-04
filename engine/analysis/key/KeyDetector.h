#pragma once

#include "engine/theory/Key.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <array>
#include <map>
#include <memory>
#include <optional>
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

    juce::String warning;            // non-blocking: e.g. sidecar unavailable, chroma used instead

    // Ensemble (< 30 s inputs with the sidecar): the chroma opinion and whether it agreed.
    std::shared_ptr<const KeyResult> ensembleChroma;
    std::optional<bool> chromaAgrees;
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

/** Runtime configuration.
    KEY_BACKEND = onnx | chroma | auto | sidecar (default onnx; without a model it falls back to chroma
    with a "run `make models`" warning; auto = onnx, else sidecar, else chroma, without warnings).
    OPENKEYSCAN_SIDECAR = executable (default: bundled, then the dev build's build/sidecar/...).
    OPENKEYSCAN_SIDECAR_ARGS = extra leading arguments, space separated (e.g. a server.py when the
    executable is a Python interpreter).
    JAUDIOS_MODELS_DIR = where openkeyscan3.onnx lives (default: see modelSearchLocations()).
    auto prefers onnx, then sidecar, then chroma. */
struct AppConfig
{
    juce::String keyBackend = "onnx";
    juce::File sidecarExecutable;
    juce::StringArray sidecarArguments;
    juce::File modelsDir;
    bool onnxCoreML = false; // JAUDIOS_ONNX_COREML=1 (macOS; gives wrong results with ORT 1.19.2)

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
    `onnx` (Phase B, M2) is not implemented yet. The sidecar process is only started on first use
    (on the Analysis thread). */
KeyDetectorHandle makeKeyDetector (const AppConfig&);

/** Where the sidecar executable is looked for when none is configured (first existing wins). */
juce::Array<juce::File> sidecarSearchLocations();

/** Where openkeyscan3.onnx is looked for when no models dir is configured (first existing wins):
    ~/Library/Application Support/JAudios/models (mac; per AGENTS.md) or the platform app-data dir,
    the app bundle's Resources/models, then the dev build's build/models (`make models`). */
juce::Array<juce::File> modelSearchLocations();

} // namespace jaudios::analysis
