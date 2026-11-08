#include "CaptureRecorder.h"

namespace jaudios
{

class CaptureRecorder::Writer final : public juce::Thread
{
public:
    explicit Writer (CaptureRecorder& r) : juce::Thread ("JAudios capture writer"), owner (r) {}
    void run() override
    {
        while (! threadShouldExit())
        {
            owner.drain();
            wait (owner.settings.writerIntervalMs);
        }
        owner.drain(); // what arrived before stop()
    }

private:
    CaptureRecorder& owner;
};

CaptureRecorder::CaptureRecorder() : CaptureRecorder (Settings {}) {}
CaptureRecorder::CaptureRecorder (Settings s) : settings (std::move (s)) {}

CaptureRecorder::~CaptureRecorder()
{
    if (writer != nullptr)
        writer->stopThread (5000);
    closeSpill();
    if (spillFile != juce::File())
        spillFile.deleteFile();
}

bool CaptureRecorder::start (double sampleRate, int numChannels, const juce::String& sourceName)
{
    if (recording.load() || sampleRate <= 0.0 || numChannels <= 0)
        return false;
    rate = sampleRate;
    channels = juce::jmin (numChannels, 2); // the analysis uses up to stereo
    source = sourceName;
    startedMs = juce::Time::currentTimeMillis();
    const int size = juce::jmax (1024, (int) (settings.ringSeconds * sampleRate));
    ring.setSize (channels, size, false, true, false);
    for (int ch = 0; ch < channels; ++ch)
        ringData[(size_t) ch] = ring.getWritePointer (ch);
    fifo.setTotalSize (size);
    fifo.reset();
    chunks.clear();
    storedSamples = 0;
    received = 0;
    dropped = 0;
    peak = 0.0f;
    spilled = false;
    recording = true;
    writer = std::make_unique<Writer> (*this);
    writer->startThread (juce::Thread::Priority::high);
    return true;
}

void CaptureRecorder::push (const float* const* in, int numChannels, int numSamples) noexcept
{
    if (! recording.load (std::memory_order_acquire) || numChannels <= 0 || numSamples <= 0)
        return;
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);
    float loudest = 0.0f;
    for (int ch = 0; ch < channels; ++ch)
    {
        const float* src = in[juce::jmin (ch, numChannels - 1)];
        if (size1 > 0)
            juce::FloatVectorOperations::copy (ringData[(size_t) ch] + start1, src, size1);
        if (size2 > 0)
            juce::FloatVectorOperations::copy (ringData[(size_t) ch] + start2, src + size1, size2);
        loudest = juce::jmax (loudest, juce::FloatVectorOperations::findMaximum (src, numSamples),
                              -juce::FloatVectorOperations::findMinimum (src, numSamples));
    }
    fifo.finishedWrite (size1 + size2);
    received.fetch_add (size1 + size2, std::memory_order_relaxed);
    if (size1 + size2 < numSamples)
        dropped.fetch_add (numSamples - size1 - size2, std::memory_order_relaxed); // the writer fell behind
    float prev = peak.load (std::memory_order_relaxed);
    while (loudest > prev && ! peak.compare_exchange_weak (prev, loudest, std::memory_order_relaxed)) {}
}

double CaptureRecorder::elapsedSeconds() const noexcept
{
    return rate > 0.0 ? (double) (received.load() + dropped.load()) / rate : 0.0;
}

void CaptureRecorder::drain()
{
    const int ready = fifo.getNumReady();
    if (ready <= 0)
        return;
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead (ready, start1, size1, start2, size2);
    juce::AudioBuffer<float> block (channels, size1 + size2);
    for (int ch = 0; ch < channels; ++ch)
    {
        if (size1 > 0)
            block.copyFrom (ch, 0, ringData[(size_t) ch] + start1, size1);
        if (size2 > 0)
            block.copyFrom (ch, size1, ringData[(size_t) ch] + start2, size2);
    }
    fifo.finishedRead (size1 + size2);

    if (spillWriter != nullptr)
        spillWriter->writeFromAudioSampleBuffer (block, 0, block.getNumSamples());
    else
    {
        storedSamples += block.getNumSamples();
        chunks.push_back (std::move (block));
        if ((double) storedSamples > settings.spillAfterSeconds * rate)
            spill();
    }
}

void CaptureRecorder::spill()
{
    spillFile = settings.tempDirectory.getNonexistentChildFile ("jaudios-capture", ".wav");
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (spillFile);
    juce::WavAudioFormat wav;
    spillWriter = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (rate)
                                                   .withNumChannels (channels)
                                                   .withBitsPerSample (32)); // float: bit-exact
    if (spillWriter == nullptr)
        return; // keep collecting in memory
    for (const auto& c : chunks)
        spillWriter->writeFromAudioSampleBuffer (c, 0, c.getNumSamples());
    chunks.clear();
    storedSamples = 0;
    spilled = true;
}

void CaptureRecorder::closeSpill()
{
    if (spillWriter != nullptr)
    {
        spillWriter->flush();
        spillWriter.reset();
    }
}

std::optional<analysis::CaptureResult> CaptureRecorder::stop (juce::String& error)
{
    if (! recording.exchange (false))
        return (error = "not recording", std::nullopt);
    writer->stopThread (10000); // drains what is left
    writer.reset();
    closeSpill();

    juce::AudioBuffer<float> audio;
    if (spilled.load())
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (new juce::FileInputStream (spillFile), true));
        if (reader == nullptr)
            return (error = "could not read the capture back from " + spillFile.getFullPathName(), std::nullopt);
        audio.setSize (channels, (int) reader->lengthInSamples);
        reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
        reader.reset();
        spillFile.deleteFile();
        spillFile = juce::File();
    }
    else
    {
        audio.setSize (channels, (int) storedSamples);
        int pos = 0;
        for (const auto& c : chunks)
        {
            for (int ch = 0; ch < channels; ++ch)
                audio.copyFrom (ch, pos, c, ch, 0, c.getNumSamples());
            pos += c.getNumSamples();
        }
        chunks.clear();
        storedSamples = 0;
    }

    int first = 0, last = audio.getNumSamples();
    if (settings.trimSilence)
    {
        // exact digital silence (a stopped transport), not quiet music
        auto silent = [&] (int i) {
            for (int ch = 0; ch < channels; ++ch)
                if (audio.getSample (ch, i) != 0.0f)
                    return false;
            return true;
        };
        while (first < last && silent (first))
            ++first;
        while (last > first && silent (last - 1))
            --last;
    }
    if (last <= first)
        return (error = "nothing but silence was captured", std::nullopt);
    juce::AudioBuffer<float> trimmed (channels, last - first);
    for (int ch = 0; ch < channels; ++ch)
        trimmed.copyFrom (ch, 0, audio, ch, first, last - first);
    return analysis::makeCaptureResult (trimmed, rate, startedMs, "Live capture: " + source);
}

} // namespace jaudios
