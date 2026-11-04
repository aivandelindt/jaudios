#include "TestSignals.h"

#include "app/core/AnalysisService.h"
#include "engine/analysis/AudioImport.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace jaudios;
using namespace jaudios::analysis;
using Catch::Matchers::WithinAbs;

namespace
{
juce::File writeFile (const juce::File& dir, const juce::String& name, juce::AudioFormat& format,
                      const juce::AudioBuffer<float>& buffer, double sampleRate, int bits)
{
    auto file = dir.getChildFile (name);
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                      .withSampleRate (sampleRate)
                                                      .withNumChannels (buffer.getNumChannels())
                                                      .withBitsPerSample (bits));
    REQUIRE (writer != nullptr);
    REQUIRE (writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()));
    return file;
}
} // namespace

TEST_CASE ("Import WAV at 44.1 kHz keeps samples, channels and metadata", "[capture][import]")
{
    test::TempDir tmp;
    juce::WavAudioFormat wav;
    auto src = test::makeSine (2, 44100, 440.0, 44100.0, 0.5f);
    const auto file = writeFile (tmp.dir, "a.wav", wav, *src, 44100.0, 24);

    AudioImporter importer;
    const auto result = importer.importFile (file);

    REQUIRE (result.ok());
    const auto& c = *result.capture;
    CHECK (c.sampleRate == analysisSampleRate);
    CHECK (c.originalSampleRate == 44100.0);
    CHECK (c.originalNumChannels == 2);
    CHECK (c.originalLengthSamples == 44100);
    CHECK (c.buffer.getNumChannels() == 2);
    CHECK (c.buffer.getNumSamples() == 44100);
    CHECK (c.sourceFile == file);
    CHECK (c.formatName.isNotEmpty());
    CHECK_THAT (c.durationSeconds(), WithinAbs (1.0, 1e-9));

    float maxErr = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 44100; ++i)
            maxErr = std::max (maxErr, std::abs (c.buffer.getSample (ch, i) - src->getSample (ch, i)));
    CHECK (maxErr < 1.0f / (1 << 22)); // 24-bit quantisation
}

TEST_CASE ("Import resamples any rate to 44.1 kHz and records the original", "[capture][import]")
{
    const double rate = GENERATE (22050.0, 48000.0, 96000.0);
    test::TempDir tmp;
    juce::WavAudioFormat wav;
    auto src = test::makeSine (1, (int) rate * 2, 1000.0, rate, 0.5f);
    const auto file = writeFile (tmp.dir, "r.wav", wav, *src, rate, 24);

    AudioImporter importer;
    const auto result = importer.importFile (file);

    REQUIRE (result.ok());
    CHECK (result.capture->sampleRate == 44100.0);
    CHECK (result.capture->originalSampleRate == rate);
    CHECK (result.capture->buffer.getNumChannels() == 1);
    CHECK (result.capture->buffer.getNumSamples() == 88200);
    CHECK_THAT (result.capture->durationSeconds(), WithinAbs (2.0, 1e-9));
}

TEST_CASE ("Import reads AIFF and FLAC identically to WAV", "[capture][import]")
{
    test::TempDir tmp;
    auto src = test::makeSine (2, 22050, 220.0, 44100.0, 0.4f);

    juce::WavAudioFormat wav;
    juce::AiffAudioFormat aiff;
    juce::FlacAudioFormat flac;
    const auto wavFile = writeFile (tmp.dir, "x.wav", wav, *src, 44100.0, 16);
    const auto aiffFile = writeFile (tmp.dir, "x.aiff", aiff, *src, 44100.0, 16);
    const auto flacFile = writeFile (tmp.dir, "x.flac", flac, *src, 44100.0, 16);

    AudioImporter importer;
    const auto a = importer.importFile (wavFile);
    const auto b = importer.importFile (aiffFile);
    const auto c = importer.importFile (flacFile);
    REQUIRE (a.ok());
    REQUIRE (b.ok());
    REQUIRE (c.ok());

    for (const auto* other : { &*b.capture, &*c.capture })
    {
        REQUIRE (other->buffer.getNumSamples() == a.capture->buffer.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            REQUIRE (std::memcmp (other->buffer.getReadPointer (ch), a.capture->buffer.getReadPointer (ch),
                                  sizeof (float) * 22050) == 0);
    }
}

TEST_CASE ("Import is deterministic (byte-identical buffers)", "[capture][import]")
{
    test::TempDir tmp;
    juce::WavAudioFormat wav;
    auto src = test::makeSine (2, 48000, 440.0, 48000.0);
    const auto file = writeFile (tmp.dir, "d.wav", wav, *src, 48000.0, 24);

    AudioImporter importer;
    const auto a = importer.importFile (file);
    const auto b = importer.importFile (file);
    REQUIRE (a.ok());
    REQUIRE (b.ok());
    for (int ch = 0; ch < 2; ++ch)
        REQUIRE (std::memcmp (a.capture->buffer.getReadPointer (ch), b.capture->buffer.getReadPointer (ch),
                              sizeof (float) * (size_t) a.capture->buffer.getNumSamples()) == 0);
}

TEST_CASE ("Import reports errors instead of crashing", "[capture][import]")
{
    test::TempDir tmp;
    AudioImporter importer;

    SECTION ("missing file")
    {
        const auto r = importer.importFile (tmp.dir.getChildFile ("nope.wav"));
        CHECK_FALSE (r.ok());
        CHECK_THAT (r.error.toStdString(), Catch::Matchers::ContainsSubstring ("not found"));
    }
    SECTION ("garbage with an audio extension")
    {
        auto f = tmp.dir.getChildFile ("junk.wav");
        f.replaceWithText ("this is not audio");
        const auto r = importer.importFile (f);
        CHECK_FALSE (r.ok());
        CHECK (r.error.isNotEmpty());
    }
    SECTION ("empty audio file")
    {
        juce::WavAudioFormat wav;
        juce::AudioBuffer<float> empty (2, 0);
        const auto f = writeFile (tmp.dir, "empty.wav", wav, empty, 44100.0, 16);
        const auto r = importer.importFile (f);
        CHECK_FALSE (r.ok());
        CHECK_THAT (r.error.toStdString(), Catch::Matchers::ContainsSubstring ("no audio"));
    }
}

TEST_CASE ("Importer advertises the formats used by drag-and-drop and the open dialog", "[capture][import]")
{
    AudioImporter importer;

    for (const char* name : { "a.wav", "a.WAV", "a.aif", "a.aiff", "a.flac", "a.ogg" })
        CHECK (importer.isSupportedFile (juce::File::getCurrentWorkingDirectory().getChildFile (name)));

    CHECK_FALSE (importer.isSupportedFile (juce::File::getCurrentWorkingDirectory().getChildFile ("a.txt")));

   #if JUCE_MAC
    CHECK (importer.isSupportedFile (juce::File::getCurrentWorkingDirectory().getChildFile ("a.mp3")));
    CHECK (importer.isSupportedFile (juce::File::getCurrentWorkingDirectory().getChildFile ("a.m4a")));
   #endif

    const auto wildcard = importer.getWildcardForAllFormats();
    CHECK (wildcard.contains ("*.wav"));
    CHECK (wildcard.contains ("*.flac"));
}

TEST_CASE ("CaptureResult is handed to the analysis thread and back", "[capture][import][threads]")
{
    test::TempDir tmp;
    juce::WavAudioFormat wav;
    auto src = test::makeSine (2, 4410, 440.0, 44100.0);
    const auto file = writeFile (tmp.dir, "t.wav", wav, *src, 44100.0, 16);

    AnalysisService service;
    AudioImporter importer;
    ImportResult received;

    service.submit<ImportResult> ([&] { return importer.importFile (file); },
                                  [&] (ImportResult r) { received = std::move (r); });
    service.waitUntilIdle();

    REQUIRE (received.ok());
    CHECK (received.capture->buffer.getNumSamples() == 4410);
}
