#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <optional>

namespace jaudios::analysis
{

/** Every analysis stage runs at this rate. */
inline constexpr double analysisSampleRate = 44100.0;

/** Audio handed from capture/import to the Analysis thread. */
struct CaptureResult
{
    juce::AudioBuffer<float> buffer;      // float, analysisSampleRate, original channel count
    double sampleRate = analysisSampleRate;
    double originalSampleRate = 0.0;
    int originalNumChannels = 0;
    juce::int64 originalLengthSamples = 0;
    juce::int64 startTimeMs = 0;          // wall-clock time the capture/import started
    juce::File sourceFile;                // empty for live captures
    juce::String formatName;              // e.g. "WAV file"

    double durationSeconds() const noexcept { return buffer.getNumSamples() / sampleRate; }
};

struct ImportResult
{
    std::optional<CaptureResult> capture;
    juce::String error;

    bool ok() const noexcept { return capture.has_value(); }
};

/** File import (audio-capture step 1). WAV/AIFF/FLAC/Ogg everywhere; MP3/M4A/CAF via CoreAudio on
    macOS. Everything is decoded to float and resampled to analysisSampleRate. Not realtime-safe:
    run on the Analysis thread. */
class AudioImporter
{
public:
    AudioImporter();

    ImportResult importFile (const juce::File& file) const;

    bool isSupportedFile (const juce::File& file) const;
    juce::String getWildcardForAllFormats() const;

private:
    juce::AudioFormatManager& formats() const;
    mutable juce::AudioFormatManager formatManager; // createReaderFor is non-const
};

} // namespace jaudios::analysis
