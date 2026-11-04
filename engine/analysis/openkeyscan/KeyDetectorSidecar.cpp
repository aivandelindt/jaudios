#include "KeyDetectorSidecar.h"

#include "engine/analysis/AudioImport.h"
#include "engine/analysis/Log.h"
#include "engine/analysis/Resampler.h"
#include "engine/analysis/key/KeyDetectorChroma.h"

namespace jaudios::analysis::openkeyscan
{

namespace
{
juce::File defaultCacheDir()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Caches/JAudios");
   #else
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("JAudios");
   #endif
}

juce::AudioBuffer<float> monoAt44k (const juce::AudioBuffer<float>& in, double sampleRate)
{
    juce::AudioBuffer<float> mono (1, in.getNumSamples());
    mono.clear();
    for (int ch = 0; ch < in.getNumChannels(); ++ch)
        mono.addFrom (0, 0, in, ch, 0, in.getNumSamples(), 1.0f / (float) std::max (1, in.getNumChannels()));
    return sampleRate == analysisSampleRate ? mono : resample (mono, sampleRate, analysisSampleRate);
}
} // namespace

KeyDetectorSidecar::KeyDetectorSidecar (SidecarConfig c) : config (std::move (c))
{
    if (config.cacheDir == juce::File())
        config.cacheDir = defaultCacheDir();

    process.onStdoutLine = [this] (const juce::String& line)
    {
        const auto msg = parseMessage (line);
        const std::lock_guard lock (mutex);
        switch (msg.kind)
        {
            case Message::Kind::ready:       ready = true; break;
            case Message::Kind::success:
            case Message::Kind::error:       replies[msg.id] = msg; break;
            case Message::Kind::unparseable: log::debug ("sidecar stdout (ignored): " + line); return;
            case Message::Kind::heartbeat:
            case Message::Kind::other:       return;
        }
        changed.notify_all();
    };
    process.onStderrLine = [] (const juce::String& line) { log::debug ("sidecar: " + line); };
    process.onStdoutClosed = [this]
    {
        const std::lock_guard lock (mutex);
        exited = true;
        changed.notify_all();
    };
}

KeyDetectorSidecar::~KeyDetectorSidecar() { shutdown(); }

void KeyDetectorSidecar::shutdown()
{
    process.stop();
    const std::lock_guard lock (mutex);
    started = ready = false;
    exited = false;
    replies.clear();
}

bool KeyDetectorSidecar::ensureRunning (juce::String& error)
{
    if (started && process.isRunning())
        return true;

    shutdown();
    if (! process.start (config.executable, config.arguments, config.environment, error))
        return false;
    started = true;

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    std::unique_lock lock (mutex);
    const bool ok = changed.wait_for (lock, std::chrono::milliseconds (config.readyTimeoutMs), [this] { return ready || exited; });
    if (! ok || ! ready)
    {
        error = exited ? juce::String ("sidecar exited before it was ready") : "sidecar not ready within "
                           + juce::String (config.readyTimeoutMs / 1000.0, 1) + " s";
        return false;
    }
    log::debug ("sidecar ready in " + juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 0) + " ms");
    return true;
}

KeyDetectorSidecar::Reply KeyDetectorSidecar::requestOnce (const juce::File& path)
{
    Reply reply;
    juce::String error;
    if (! ensureRunning (error))
    {
        reply.message = error;
        return reply;
    }

    const auto id = juce::Uuid().toString();
    if (! process.writeLine (makeRequest (id, path)))
    {
        reply.message = "sidecar stdin closed";
        return reply;
    }

    std::unique_lock lock (mutex);
    const bool answered = changed.wait_for (lock, std::chrono::milliseconds (config.requestTimeoutMs),
                                            [&] { return replies.count (id) > 0 || exited; });
    if (! answered || replies.count (id) == 0)
    {
        reply.message = exited ? juce::String ("sidecar crashed") : "sidecar timed out after "
                              + juce::String (config.requestTimeoutMs / 1000.0, 1) + " s";
        return reply;
    }

    const auto msg = replies[id];
    replies.erase (id);
    if (msg.kind == Message::Kind::success)
    {
        reply.status = Reply::Status::ok;
        reply.key = msg.key;
    }
    else
    {
        reply.status = Reply::Status::serverError;
        reply.message = msg.error;
    }
    return reply;
}

KeyDetectorSidecar::Reply KeyDetectorSidecar::requestWithRestart (const juce::File& path)
{
    if (fallenBack)
        return { Reply::Status::failed, {}, fallbackReason };

    auto reply = requestOnce (path);
    if (reply.status != Reply::Status::failed)
        return reply;

    log::warning ("key detection sidecar failed (" + reply.message + ")");
    shutdown();

    if (! restartUsed)
    {
        restartUsed = true;
        reply = requestOnce (path);
        if (reply.status != Reply::Status::failed)
            return reply;
        shutdown();
    }

    fallenBack = true;
    fallbackReason = reply.message;
    log::warning ("key detection sidecar disabled for this session: " + fallbackReason);
    return reply;
}

KeyResult KeyDetectorSidecar::fromSidecar (theory::Key key) const
{
    KeyResult r;
    r.backend = KeyBackend::sidecar;
    r.scoresAvailable = false;
    r.top3.push_back ({ key, theory::camelotNumber (key), theory::camelotLetter (key), 1.0f });
    r.margin = 1.0f;
    r.lowConfidence = false;
    r.scores.fill (0.0f);
    r.scores[(size_t) theory::index24 (key)] = 1.0f;
    return r;
}

KeyResult KeyDetectorSidecar::chromaFor (const juce::AudioBuffer<float>& audio, double sampleRate,
                                         const juce::String& warning) const
{
    KeyDetectorChroma chroma;
    auto r = chroma.analyzeBuffer (audio, sampleRate);
    r.warning = warning;
    return r;
}

KeyResult KeyDetectorSidecar::withEnsemble (KeyResult sidecar, const juce::AudioBuffer<float>& audio, double sampleRate) const
{
    if (audio.getNumSamples() >= config.ensembleBelowSec * sampleRate)
        return sidecar;

    auto chroma = std::make_shared<KeyResult> (chromaFor (audio, sampleRate, {}));
    const bool agree = ! chroma->top3.empty() && chroma->top3[0].key == sidecar.top3[0].key;
    sidecar.chromaAgrees = agree;
    sidecar.ensembleChroma = chroma;

    // Skill rule: on disagreement prefer the model unless its margin < 0.15; then chroma's key
    // becomes the runner-up and the result is low-confidence. (Phase A has no scores: margin 1.)
    if (! agree && sidecar.margin < lowConfidenceMargin && ! chroma->top3.empty())
    {
        sidecar.top3.resize (1);
        sidecar.top3.push_back (chroma->top3[0]);
        sidecar.lowConfidence = true;
    }
    return sidecar;
}

KeyResult KeyDetectorSidecar::analyzeFile (const juce::File& file)
{
    const AudioImporter importer;
    auto imported = importer.importFile (file);

    const auto reply = requestWithRestart (file.getFullPathName().isNotEmpty() ? juce::File (file.getFullPathName()) : file);

    if (reply.status == Reply::Status::ok)
        return imported.ok() ? withEnsemble (fromSidecar (reply.key), imported.capture->buffer, imported.capture->sampleRate)
                             : fromSidecar (reply.key);

    if (! imported.ok())
    {
        KeyResult empty = KeyDetectorChroma::scoreChroma ({});
        empty.warning = imported.error;
        return empty;
    }

    const auto warning = reply.status == Reply::Status::serverError
                           ? "Key detection sidecar could not analyse this file (" + reply.message + "); used chroma"
                           : "Key detection sidecar unavailable (" + reply.message + "); using chroma";
    return chromaFor (imported.capture->buffer, imported.capture->sampleRate, warning);
}

KeyResult KeyDetectorSidecar::analyzeBuffer (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    if (fallenBack)
        return chromaFor (buffer, sampleRate, "Key detection sidecar unavailable (" + fallbackReason + "); using chroma");

    // Skill: temp WAV, 44.1 kHz 16-bit PCM mono, in the app cache dir, deleted after the response.
    config.cacheDir.createDirectory();
    const auto temp = config.cacheDir.getNonexistentChildFile ("key-" + juce::Uuid().toString(), ".wav");
    {
        const auto mono = monoAt44k (buffer, sampleRate);
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (temp);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (analysisSampleRate)
                                                       .withNumChannels (1)
                                                       .withBitsPerSample (16));
        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (mono, 0, mono.getNumSamples()))
        {
            temp.deleteFile();
            return chromaFor (buffer, sampleRate, "Could not write a temporary file for the key detection sidecar; used chroma");
        }
    }

    const auto reply = requestWithRestart (temp);
    temp.deleteFile();

    if (reply.status == Reply::Status::ok)
        return withEnsemble (fromSidecar (reply.key), buffer, sampleRate);

    const auto warning = reply.status == Reply::Status::serverError
                           ? "Key detection sidecar could not analyse this audio (" + reply.message + "); used chroma"
                           : "Key detection sidecar unavailable (" + reply.message + "); using chroma";
    return chromaFor (buffer, sampleRate, warning);
}

} // namespace jaudios::analysis::openkeyscan
