#pragma once

#include "OpenKeyScanProtocol.h"
#include "SidecarProcess.h"

#include "engine/analysis/key/KeyDetector.h"

#include <condition_variable>
#include <map>
#include <mutex>

namespace jaudios::analysis::openkeyscan
{

struct SidecarConfig
{
    juce::File executable;
    juce::StringArray arguments;                        // full argument list (incl. -w / --device)
    std::map<juce::String, juce::String> environment;   // added to the inherited environment
    int readyTimeoutMs = 20000;                         // skill: 20 s to "ready"
    int requestTimeoutMs = 30000;                       // skill: 30 s per request
    juce::File cacheDir;                                // temp WAVs for analyzeBuffer (default: app cache)
    double ensembleBelowSec = 30.0;                     // shorter inputs also run chroma
};

/** Phase A key backend: OpenKeyScan analyzer as a long-lived child process (NDJSON over stdio).

    - Started lazily on first use (Analysis thread), kept alive for the session.
    - The server returns only the argmax class: top3 = [that key] with score 1, margin 1,
      scoresAvailable = false (the UI hides alternatives). Phase B (ONNX) gives real scores.
    - Crash / ready timeout / request timeout: kill, restart once (retrying the request); if that
      fails too, fall back to KeyDetectorChroma for the rest of the session with a warning.
    - A server-side error for one file uses chroma for that file only.
    - Inputs shorter than 30 s also run chroma (ensemble rule from the skill).
    - stderr goes to the app log at debug level.
*/
class KeyDetectorSidecar final : public IKeyDetector
{
public:
    explicit KeyDetectorSidecar (SidecarConfig);
    ~KeyDetectorSidecar() override;

    KeyResult analyzeFile (const juce::File&) override;
    KeyResult analyzeBuffer (const juce::AudioBuffer<float>&, double sampleRate) override;

    bool hasFallenBack() const noexcept { return fallenBack; }

private:
    struct Reply
    {
        enum class Status { ok, serverError, failed } status = Status::failed;
        theory::Key key;
        juce::String message;
    };

    Reply requestWithRestart (const juce::File& absolutePath);
    Reply requestOnce (const juce::File& absolutePath);
    bool ensureRunning (juce::String& error);
    void shutdown();

    KeyResult fromSidecar (theory::Key) const;
    KeyResult chromaFor (const juce::AudioBuffer<float>&, double sampleRate, const juce::String& warning) const;
    KeyResult withEnsemble (KeyResult sidecar, const juce::AudioBuffer<float>&, double sampleRate) const;

    SidecarConfig config;
    SidecarProcess process;
    bool started = false, fallenBack = false, restartUsed = false;
    juce::String fallbackReason;

    std::mutex mutex;
    std::condition_variable changed;
    bool ready = false, exited = false;
    std::map<juce::String, Message> replies; // by request id
};

} // namespace jaudios::analysis::openkeyscan
