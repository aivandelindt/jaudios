// audio-capture (live): the recorder (lock-free ring -> writer thread -> memory or temp file),
// input-device capture, and the acceptance that a live capture and a file import of the same
// source give byte-identical buffers and analysis.
#include "TestSignals.h"

#include "app/core/CaptureSources.h"
#include "app/core/RealtimeTrap.h"
#include "engine/analysis/Analysis.h"
#include "engine/analysis/key/KeyDetector.h"

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <set>
#include <thread>

using namespace jaudios;

namespace
{
/** Feeds `audio` into the recorder from another thread in random block sizes, like a device.
    `speed` > 0 paces it like a device running that many times faster than realtime (a real device
    is paced by its clock; 0 = as fast as possible, to overflow the ring on purpose). */
void feed (CaptureRecorder& rec, const juce::AudioBuffer<float>& audio, unsigned seed = 1, int minBlock = 32, int maxBlock = 1024,
           double speed = 20.0, double sampleRate = 44100.0)
{
    std::thread device ([&] {
        std::mt19937 rng (seed);
        std::uniform_int_distribution<int> size (minBlock, maxBlock);
        const float* ptrs[2];
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int pos = 0; pos < audio.getNumSamples();)
        {
            const int n = std::min (size (rng), audio.getNumSamples() - pos);
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                ptrs[ch] = audio.getReadPointer (ch, pos);
            rec.push (ptrs, audio.getNumChannels(), n);
            pos += n;
            if (speed > 0.0)
            {
                const double due = t0 + pos / sampleRate * 1000.0 / speed;
                while (juce::Time::getMillisecondCounterHiRes() < due)
                    std::this_thread::sleep_for (std::chrono::microseconds (200));
            }
        }
    });
    device.join();
}

bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) != 0)
            return false;
    return true;
}

/** A 48 kHz stereo song: the house fixture, resampled (a capture device's usual rate). */
juce::AudioBuffer<float> song48k()
{
    analysis::AudioImporter importer;
    auto imported = importer.importFile (juce::File (JAUDIOS_SOURCE_DIR).getChildFile ("tests/fixtures/house_124_Amin.wav"));
    REQUIRE (imported.ok());
    const auto& src = imported.capture->buffer;
    const int n = (int) (src.getNumSamples() * 48000.0 / 44100.0);
    juce::AudioBuffer<float> out (2, n);
    for (int ch = 0; ch < 2; ++ch)
    {
        juce::LagrangeInterpolator interp;
        interp.process (44100.0 / 48000.0, src.getReadPointer (std::min (ch, src.getNumChannels() - 1)), out.getWritePointer (ch), n);
    }
    return out;
}
} // namespace

TEST_CASE ("Recorder: a capture thread's blocks come back sample-exact", "[capture]")
{
    CaptureRecorder rec;
    auto audio = test::makeSine (2, 44100 * 3, 330.0, 44100.0, 0.5f);
    REQUIRE (rec.start (44100.0, 2, "test"));
    CHECK (rec.isRecording());
    feed (rec, *audio, 7, 32, 1024);
    CHECK (rec.elapsedSeconds() > 2.99);
    CHECK (rec.takePeak() > 0.49f);
    juce::String error;
    const auto result = rec.stop (error);
    INFO (error);
    REQUIRE (result.has_value());
    CHECK (rec.droppedSamples() == 0);
    CHECK (result->originalSampleRate == 44100.0);
    CHECK (result->formatName == "Live capture: test");
    CHECK (identical (result->buffer, *audio)); // 44.1 kHz in: no resampling, exact
    CHECK_FALSE (rec.isRecording());
}

TEST_CASE ("Recorder: exact digital silence before and after the music is trimmed", "[capture]")
{
    CaptureRecorder rec;
    juce::AudioBuffer<float> audio (2, 30000);
    audio.clear();
    for (int i = 10000; i < 20000; ++i)
        for (int ch = 0; ch < 2; ++ch)
            audio.setSample (ch, i, 0.1f * std::sin ((float) i * 0.05f));
    REQUIRE (rec.start (44100.0, 2, "test"));
    feed (rec, audio);
    juce::String error;
    const auto r = rec.stop (error);
    REQUIRE (r.has_value());
    CHECK (r->buffer.getNumSamples() == 10000); // the music only
    REQUIRE (rec.start (44100.0, 2, "silence"));
    juce::AudioBuffer<float> silent (2, 4410);
    silent.clear();
    feed (rec, silent);
    CHECK_FALSE (rec.stop (error).has_value());
    CHECK (error.contains ("silence"));
}

TEST_CASE ("Recorder: long captures stream to a temp file, with the same result", "[capture]")
{
    auto audio = test::makeSine (2, 44100 * 4, 220.0, 44100.0, 0.4f);
    CaptureRecorder::Settings s;
    s.spillAfterSeconds = 0.5; // 5 minutes in the app
    test::TempDir tmp;
    s.tempDirectory = tmp.dir;
    CaptureRecorder rec (s);
    REQUIRE (rec.start (44100.0, 2, "long"));
    feed (rec, *audio, 3, 64, 2048);
    juce::String error;
    const auto r = rec.stop (error);
    REQUIRE (r.has_value());
    CHECK (rec.droppedSamples() == 0);
    CHECK (rec.spilledToFile());
    CHECK (identical (r->buffer, *audio));
    CHECK (tmp.dir.getNumberOfChildFiles (juce::File::findFiles) == 0); // the temp file is gone
}

TEST_CASE ("Recorder: a writer that falls behind drops samples and says how many", "[capture]")
{
    CaptureRecorder::Settings s;
    s.ringSeconds = 0.05;       // a tiny ring...
    s.writerIntervalMs = 500;   // ...drained rarely
    CaptureRecorder rec (s);
    auto audio = test::makeSine (2, 44100, 220.0, 44100.0, 0.4f);
    REQUIRE (rec.start (44100.0, 2, "slow"));
    feed (rec, *audio, 5, 512, 512, 0.0); // a second of audio at once
    juce::String error;
    const auto r = rec.stop (error);
    CHECK (rec.droppedSamples() > 0);
    REQUIRE (r.has_value());
    CHECK (r->buffer.getNumSamples() + rec.droppedSamples() >= 44100 - 1);
}

TEST_CASE ("Input-device capture: the device callback feeds the recorder at the device's rate", "[capture]")
{
    // a device that never opens: the test drives the callbacks as a device would
    struct FakeDevice final : juce::AudioIODevice
    {
        FakeDevice() : juce::AudioIODevice ("Loopback", "Fake") {}
        juce::StringArray getOutputChannelNames() override { return {}; }
        juce::StringArray getInputChannelNames() override { return { "L", "R" }; }
        juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
        juce::Array<int> getAvailableBufferSizes() override { return { 256 }; }
        int getDefaultBufferSize() override { return 256; }
        juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
        void close() override {}
        bool isOpen() override { return true; }
        void start (juce::AudioIODeviceCallback*) override {}
        void stop() override {}
        bool isPlaying() override { return true; }
        juce::String getLastError() override { return {}; }
        int getCurrentBufferSizeSamples() override { return 256; }
        double getCurrentSampleRate() override { return 48000.0; }
        int getCurrentBitDepth() override { return 24; }
        juce::BigInteger getActiveOutputChannels() const override { return {}; }
        juce::BigInteger getActiveInputChannels() const override { juce::BigInteger b; b.setRange (0, 2, true); return b; }
        int getOutputLatencyInSamples() override { return 0; }
        int getInputLatencyInSamples() override { return 0; }
    } device;
    CaptureRecorder rec;
    DeviceCaptureSource source ("Loopback");
    source.attach (rec);
    source.audioDeviceAboutToStart (&device);
    REQUIRE (rec.isRecording());
    CHECK (rec.sampleRate() == 48000.0);
    auto audio = test::makeSine (2, 48000, 440.0, 48000.0, 0.3f);
    for (int pos = 0; pos < 48000; pos += 256)
    {
        const float* in[2] = { audio->getReadPointer (0, pos), audio->getReadPointer (1, pos) };
        source.audioDeviceIOCallbackWithContext (in, 2, nullptr, 0, std::min (256, 48000 - pos), {});
    }
    juce::String error;
    const auto r = rec.stop (error);
    REQUIRE (r.has_value());
    CHECK (r->originalSampleRate == 48000.0);
    CHECK (r->sampleRate == 44100.0);
    CHECK (std::abs (r->buffer.getNumSamples() - 44100) <= 2);
}

TEST_CASE ("Capture sources: ids are unique; unknown ids are refused", "[capture]")
{
    juce::String error;
    const auto sources = listCaptureSources (error);
    std::set<juce::String> ids;
    for (const auto& s : sources)
    {
        CHECK (s.id.isNotEmpty());
        CHECK (ids.insert (s.id).second);
    }
    CHECK (makeCaptureSource ("bogus", error) == nullptr);
    CHECK (error.contains ("unknown"));
    CHECK (makeCaptureSource ("device:Some input", error) != nullptr);
#if ! JUCE_MAC
    CHECK (makeCaptureSource ("system", error) == nullptr); // ScreenCaptureKit is macOS only
#endif
}

TEST_CASE ("A live capture and a file import of the same source analyse byte-identically", "[capture][acceptance]")
{
    // the same 48 kHz source as a file and as a capture stream
    test::TempDir tmp;
    const auto source = song48k();
    const auto wav = tmp.dir.getChildFile ("song48k.wav");
    {
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (wav);
        juce::WavAudioFormat fmt;
        auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (32));
        REQUIRE (w != nullptr);
        REQUIRE (w->writeFromAudioSampleBuffer (source, 0, source.getNumSamples()));
    }
    analysis::AudioImporter importer;
    const auto imported = importer.importFile (wav);
    REQUIRE (imported.ok());

    // Trimming is off here: it removes exact digital silence, which a capture has (a stopped
    // transport) and a file may have too (this one starts on a 0.0 sample). The capture-only trim
    // is tested above; this checks that the same samples give the same buffer.
    CaptureRecorder::Settings settings;
    settings.trimSilence = false;
    CaptureRecorder rec (settings);
    REQUIRE (rec.start (48000.0, 2, "song"));
    feed (rec, source, 11, 128, 1024, 20.0, 48000.0);
    juce::String error;
    const auto captured = rec.stop (error);
    REQUIRE (captured.has_value());
    CHECK (rec.droppedSamples() == 0);
    INFO ("captured " << captured->buffer.getNumSamples() << " samples, imported " << imported.capture->buffer.getNumSamples());
    REQUIRE (identical (captured->buffer, imported.capture->buffer));

    analysis::AppConfig config;
    config.keyBackend = "chroma"; // the analysis itself is the same either way; chroma needs no model
    auto detector = analysis::makeKeyDetector (config);
    auto a = analysis::analyze (*imported.capture, *detector.detector);
    auto b = analysis::analyze (*captured, *detector.detector);
    a.keyMs = b.keyMs = a.rhythmMs = b.rhythmMs = 0.0; // wall-clock timings differ run to run
    // the whole analysis as app-cli prints it, with the same file metadata on both sides
    CHECK (juce::JSON::toString (analysis::toJson (a, *imported.capture)) == juce::JSON::toString (analysis::toJson (b, *imported.capture)));
    CHECK (a.rhythm.beats == b.rhythm.beats);
    CHECK (a.rhythm.bpm == b.rhythm.bpm);
}

TEST_CASE ("The capture callback's push() allocates nothing", "[capture][rt]")
{
    if (! rt::isTrapInstalled())
        SKIP ("realtime trap not linked; run tools/rt_check.sh");
    CaptureRecorder rec;
    REQUIRE (rec.start (48000.0, 2, "rt"));
    auto audio = test::makeSine (2, 4800, 440.0, 48000.0, 0.3f);
    rt::resetStats();
    for (int pos = 0; pos < 4800; pos += 480)
    {
        const float* in[2] = { audio->getReadPointer (0, pos), audio->getReadPointer (1, pos) };
        const rt::ScopedRealtimeSection section;
        rec.push (in, 2, 480);
    }
    CHECK (rt::getStats().allocations == 0);
    CHECK (rt::getStats().deallocations == 0);
    juce::String error;
    rec.stop (error);
}
