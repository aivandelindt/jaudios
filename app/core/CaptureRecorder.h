#pragma once

#include "engine/analysis/AudioImport.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <optional>

namespace jaudios
{

/** Live capture into a CaptureResult (audio-capture step 4). The capture callback (an input device,
    ScreenCaptureKit) push()es into a lock-free ring: no allocation, no lock, no logging there. A
    writer thread drains the ring into memory and, past `spillAfterSeconds`, into a temp WAV file,
    so a capture has no length limit (step 6). stop() hands back the audio through
    makeCaptureResult(), the same conversion file import uses. */
class CaptureRecorder
{
public:
    struct Settings
    {
        double ringSeconds = 4.0;          // headroom if the writer thread is late
        double spillAfterSeconds = 300.0;  // skill: stream to a temp file past 5 minutes
        bool trimSilence = true;           // drop exact digital silence before / after the music
        int writerIntervalMs = 10;
        juce::File tempDirectory = juce::File::getSpecialLocation (juce::File::tempDirectory);
    };

    CaptureRecorder();
    explicit CaptureRecorder (Settings);
    ~CaptureRecorder();

    /** Message thread (or a device's aboutToStart): allocates the ring, starts the writer. */
    bool start (double sampleRate, int numChannels, const juce::String& sourceName);
    /** Message thread: stops the writer, collects everything and converts it. Empty (with an error)
        when nothing but silence arrived. */
    std::optional<analysis::CaptureResult> stop (juce::String& error);
    bool isRecording() const noexcept { return recording.load(); }

    /** Capture thread (realtime). Channels beyond start()'s count are ignored; fewer are repeated. */
    void push (const float* const* channels, int numChannels, int numSamples) noexcept;

    // --- meters (any thread) ---------------------------------------------------------------
    double elapsedSeconds() const noexcept;
    /** The loudest sample since the last call (a level meter polls this). */
    float takePeak() noexcept { return peak.exchange (0.0f); }
    juce::int64 droppedSamples() const noexcept { return dropped.load(); }
    bool spilledToFile() const noexcept { return spilled.load(); }
    double sampleRate() const noexcept { return rate; }

private:
    class Writer;
    void drain();               // writer thread
    void spill();               // writer thread: memory -> temp file
    void closeSpill();

    Settings settings;
    double rate = 0.0;
    int channels = 0;
    juce::String source;
    juce::int64 startedMs = 0;

    juce::AbstractFifo fifo { 1 };
    juce::AudioBuffer<float> ring;
    std::array<float*, 2> ringData {}; // raw pointers: AudioBuffer's accessors write its isClear flag (a race)
    std::atomic<bool> recording { false };
    std::atomic<juce::int64> received { 0 }, dropped { 0 };
    std::atomic<float> peak { 0.0f };
    std::atomic<bool> spilled { false };

    // writer thread only (until stop() joins it)
    std::unique_ptr<Writer> writer;
    std::vector<juce::AudioBuffer<float>> chunks;
    juce::int64 storedSamples = 0;
    juce::File spillFile;
    std::unique_ptr<juce::AudioFormatWriter> spillWriter;
};

} // namespace jaudios
