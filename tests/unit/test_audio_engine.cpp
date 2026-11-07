#include "TestSignals.h"

#include "app/core/AudioEngine.h"
#include "app/core/OfflineRender.h"
#include "app/core/RealtimeTrap.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace jaudios;
using Catch::Matchers::WithinAbs;

TEST_CASE ("AudioEngine plays a buffer through the graph bit-exactly at unity gain", "[scaffold][engine]")
{
    AudioEngine engine;
    auto source = test::makeSine (2, 44100, 440.0, 44100.0);

    const auto report = renderThroughEngine (engine, source, 44100.0, 128);

    CHECK (report.numBlocks == (44100 + 127) / 128);
    CHECK (report.output.getNumSamples() == 44100);
    CHECK (report.maxAbsDiffVsSource == 0.0f);
}

TEST_CASE ("AudioEngine output is identical when the device delivers varying block sizes", "[scaffold][engine]")
{
    AudioEngine engine;
    auto source = test::makeSine (2, 20000, 330.0, 44100.0);

    const auto report = renderThroughEngine (engine, source, 44100.0, 128, { 100, 37, 128, 300, 1 });

    CHECK (report.maxAbsDiffVsSource == 0.0f);
}

TEST_CASE ("AudioEngine duplicates a mono source to both outputs", "[scaffold][engine]")
{
    AudioEngine engine;
    auto source = test::makeSine (1, 4096, 1000.0, 48000.0);

    const auto report = renderThroughEngine (engine, source, 48000.0, 128);

    CHECK (report.maxAbsDiffVsSource == 0.0f);
    for (int i = 0; i < 4096; ++i)
        REQUIRE (report.output.getSample (1, i) == report.output.getSample (0, i));
}

TEST_CASE ("AudioEngine latency at 128 samples is below 10 ms", "[scaffold][engine][latency]")
{
    AudioEngine engine;
    engine.prepareOffline (44100.0, 128);
    // 128 / 44100 = 2.90 ms; the graph adds no latency. Device-reported output latency is added
    // on top when a device is open (verify on hardware with the GUI app's status line).
    CHECK (engine.getOutputLatencyMs() < 10.0);
    CHECK_THAT (engine.getOutputLatencyMs(), WithinAbs (1000.0 * 128 / 44100.0, 1e-9));
}

TEST_CASE ("AudioEngine mute and solo reach silence after the gain ramp", "[scaffold][engine]")
{
    AudioEngine engine;
    auto source = test::makeSine (2, 44100, 440.0, 44100.0);

    SECTION ("mute")
    {
        engine.setChannelMute (ChannelId::source, true);
    }
    SECTION ("solo another channel")
    {
        engine.setChannelSolo (ChannelId::chords, true);
    }

    const auto report = renderThroughEngine (engine, source, 44100.0, 128);

    // 20 ms ramp -> fully silent after ~882 samples.
    float tailPeak = 0.0f;
    for (int i = 2048; i < report.output.getNumSamples(); ++i)
        tailPeak = std::max (tailPeak, std::abs (report.output.getSample (0, i)));
    CHECK (tailPeak == 0.0f);
}

TEST_CASE ("AudioEngine applies channel gain without discontinuities", "[scaffold][engine]")
{
    AudioEngine engine;
    auto dc = std::make_shared<juce::AudioBuffer<float>> (2, 8192);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (dc->getWritePointer (ch), 0.5f, 8192);

    engine.setChannelGainDecibels (ChannelId::source, -6.0f);
    const auto report = renderThroughEngine (engine, dc, 44100.0, 128);

    float maxStep = 0.0f;
    for (int i = 1; i < 8192; ++i)
        maxStep = std::max (maxStep, std::abs (report.output.getSample (0, i) - report.output.getSample (0, i - 1)));

    CHECK (maxStep < 0.001f);
    CHECK_THAT (report.output.getSample (0, 8191), WithinAbs (0.5 * juce::Decibels::decibelsToGain (-6.0), 1e-4));
}

TEST_CASE ("Audio path performs zero allocations in realtime sections", "[scaffold][engine][rt]")
{
    if (! rt::isTrapInstalled())
        SKIP ("realtime trap not linked; run tools/rt_check.sh");

    SECTION ("trap self-test: an allocation inside a section is counted")
    {
        rt::resetStats();
        {
            const rt::ScopedRealtimeSection section;
            // volatile function pointer: stops the compiler eliding the new/delete pair
            static void* (*volatile allocate) (std::size_t) = ::operator new;
            static void (*volatile release) (void*) = ::operator delete;
            release (allocate (16));
        }
        CHECK (rt::getStats().allocations >= 1);

       #if defined(__GLIBC__)
        rt::resetStats();
        {
            juce::CriticalSection cs;
            const rt::ScopedRealtimeSection section;
            const juce::ScopedLock lock (cs);
        }
        CHECK (rt::getStats().mutexLocks >= 1);
       #endif
    }

    SECTION ("engine render")
    {
        AudioEngine engine;
        auto source = test::makeSine (2, 44100 * 2, 440.0, 44100.0);
        engine.setChannelGainDecibels (ChannelId::source, -3.0f);

        // Varying device block sizes (incl. a partial last block) exercise the fixed-block adapter.
        const auto report = renderThroughEngine (engine, source, 44100.0, 128, { 128, 128, 64, 200, 17 });

        CHECK (report.rtStats.allocations == 0);
        CHECK (report.rtStats.deallocations == 0);
        // Reported, not asserted: JUCE's AudioProcessorGraph takes each node's (uncontended)
        // callback lock. See tools/rt_check.sh output.
        WARN ("mutex locks on audio thread: " << report.rtStats.mutexLocks);
    }
}

namespace
{
/** Renders `n` samples through a prepared engine (fixed 128-sample device blocks). */
juce::AudioBuffer<float> renderEngine (AudioEngine& engine, int n)
{
    juce::AudioBuffer<float> out (2, n), block (2, 128);
    for (int pos = 0; pos < n; pos += 128)
    {
        const int m = std::min (128, n - pos);
        engine.renderBlock (block.getArrayOfWritePointers(), 2, m);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, block, ch, 0, m);
    }
    return out;
}
} // namespace

TEST_CASE ("AudioEngine plays stems in place of the source, with per-stem solo/mute/gain", "[engine][stems]")
{
    constexpr int n = 8192;
    auto source = std::make_shared<juce::AudioBuffer<float>> (2, n);
    auto stems = std::make_shared<juce::AudioBuffer<float>> (8, n);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (source->getWritePointer (ch), 1.0f, n); // must not be heard
    const float level[] = { 0.1f, 0.02f, 0.004f, 0.0008f };                    // drums, bass, other, vocals
    for (int s = 0; s < 4; ++s)
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (stems->getWritePointer (2 * s + ch), level[s], n);

    AudioEngine engine;
    engine.prepareOffline (44100.0, 128);
    REQUIRE (engine.setSource (source));
    REQUIRE (engine.setStems (stems));
    CHECK (engine.hasStems());
    CHECK (stemChannel (3) == ChannelId::stemVocals);

    float expected = 0.1f + 0.02f + 0.004f + 0.0008f;
    SECTION ("all stems, source replaced") {}
    SECTION ("solo other")
    {
        engine.setChannelSolo (stemChannel (2), true);
        expected = 0.004f;
    }
    SECTION ("mute drums, bass -6 dB")
    {
        engine.setChannelMute (ChannelId::stemDrums, true);
        engine.setChannelGainDecibels (ChannelId::stemBass, -6.0f);
        expected = 0.02f * juce::Decibels::decibelsToGain (-6.0f) + 0.004f + 0.0008f;
    }
    SECTION ("soloing the source plays the source")
    {
        engine.setChannelSolo (ChannelId::source, true);
        expected = 1.0f;
    }

    // the gain smoother starts at the strip's state when prepared; check after the 20 ms ramp
    engine.getSourcePlayer().play();
    const auto out = renderEngine (engine, n);
    CHECK_THAT (out.getSample (0, n - 1), WithinAbs (expected, 1e-5));
    CHECK_THAT (out.getSample (1, n - 1), WithinAbs (expected, 1e-5));
}

TEST_CASE ("AudioEngine: a new source drops the stems and un-mutes the source", "[engine][stems]")
{
    constexpr int n = 4096;
    auto source = std::make_shared<juce::AudioBuffer<float>> (2, n);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (source->getWritePointer (ch), 0.25f, n);
    auto stems = std::make_shared<juce::AudioBuffer<float>> (8, n);
    stems->clear();

    AudioEngine engine;
    engine.prepareOffline (44100.0, 128);
    engine.setSource (source);
    engine.setStems (stems);
    engine.setSource (source);
    CHECK_FALSE (engine.hasStems());
    engine.getSourcePlayer().play();
    const auto out = renderEngine (engine, n);
    CHECK_THAT (out.getSample (0, n - 1), WithinAbs (0.25, 1e-6));
}

TEST_CASE ("Stem playback performs zero allocations in realtime sections", "[engine][stems][rt]")
{
    if (! rt::isTrapInstalled())
        SKIP ("realtime trap not linked; run tools/rt_check.sh");
    constexpr int n = 44100;
    auto source = test::makeSine (2, n, 440.0, 44100.0);
    auto stems = std::make_shared<juce::AudioBuffer<float>> (8, n);
    for (int c = 0; c < 8; ++c)
        stems->copyFrom (c, 0, *source, c % 2, 0, n);

    AudioEngine engine;
    engine.prepareOffline (44100.0, 128);
    engine.setSource (source);
    engine.setStems (stems);
    engine.setChannelSolo (ChannelId::stemVocals, true);
    engine.getSourcePlayer().play();

    juce::AudioBuffer<float> block (2, 128);
    rt::resetStats();
    for (int pos = 0; pos < n; pos += 128)
        engine.renderBlock (block.getArrayOfWritePointers(), 2, 128);
    CHECK (rt::getStats().allocations == 0);
    CHECK (rt::getStats().deallocations == 0);
}
