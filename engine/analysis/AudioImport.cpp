#include "AudioImport.h"

#include "Resampler.h"

namespace jaudios::analysis
{

AudioImporter::AudioImporter()
{
    formatManager.registerBasicFormats();
}

juce::AudioFormatManager& AudioImporter::formats() const { return formatManager; }

juce::String AudioImporter::getWildcardForAllFormats() const
{
    return formats().getWildcardForAllFormats();
}

bool AudioImporter::isSupportedFile (const juce::File& file) const
{
    const auto ext = file.getFileExtension().toLowerCase();
    if (ext.isEmpty())
        return false;

    for (auto* format : formats())
        if (format->getFileExtensions().contains (ext, true))
            return true;

    return false;
}

ImportResult AudioImporter::importFile (const juce::File& file) const
{
    ImportResult result;
    const auto startTime = juce::Time::currentTimeMillis();

    if (! file.existsAsFile())
    {
        result.error = "File not found: " + file.getFullPathName();
        return result;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formats().createReaderFor (file));
    if (reader == nullptr)
    {
        result.error = "Unsupported or unreadable audio file: " + file.getFileName();
        return result;
    }

    if (reader->lengthInSamples <= 0 || reader->numChannels == 0 || reader->sampleRate <= 0.0)
    {
        result.error = "File contains no audio: " + file.getFileName();
        return result;
    }

    if (reader->lengthInSamples > std::numeric_limits<int>::max())
    {
        result.error = "File too long to import: " + file.getFileName();
        return result;
    }

    const int numChannels = (int) reader->numChannels;
    const int length = (int) reader->lengthInSamples;

    juce::AudioBuffer<float> decoded (numChannels, length);
    if (! reader->read (&decoded, 0, length, 0, true, true))
    {
        result.error = "Failed to decode: " + file.getFileName();
        return result;
    }

    result.capture = makeCaptureResult (decoded, reader->sampleRate, startTime, reader->getFormatName(), file);
    return result;
}

CaptureResult makeCaptureResult (const juce::AudioBuffer<float>& raw, double sampleRate, juce::int64 startTimeMs,
                                 const juce::String& formatName, const juce::File& sourceFile)
{
    CaptureResult capture;
    capture.originalSampleRate = sampleRate;
    capture.originalNumChannels = raw.getNumChannels();
    capture.originalLengthSamples = raw.getNumSamples();
    capture.formatName = formatName;
    capture.sourceFile = sourceFile;
    capture.startTimeMs = startTimeMs;
    capture.sampleRate = analysisSampleRate;
    capture.buffer = resample (raw, sampleRate, analysisSampleRate);
    return capture;
}

} // namespace jaudios::analysis
