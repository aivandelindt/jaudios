#include "TestSignals.h"

#include "app/core/AudioEngine.h"
#include "app/core/OfflineRender.h"
#include "app/core/RealtimeTrap.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace jaudios;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

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

//==============================================================================
// synth-and-vst3-host: generated channels (clip player + instrument) on the engine's transport.
#include "engine/synth/SfzInstrument.h"

namespace
{
/** Test instrument: every note-on writes a single 1.0 impulse at its sample (left), nothing else. */
struct ImpulseInstrument final : synth::Instrument
{
    juce::String name() const override { return "impulse"; }
    void prepare (double, int) override { prepared = true; }
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) noexcept override
    {
        out.clear();
        for (const auto m : midi)
            if (m.getMessage().isNoteOn())
                out.setSample (0, m.samplePosition, 1.0f);
    }
    void allNotesOff() noexcept override { ++notesOff; }
    bool prepared = false;
    int notesOff = 0;
};

std::shared_ptr<const MidiClip> clipAt (std::initializer_list<juce::int64> positions, int note = 60)
{
    auto c = std::make_shared<MidiClip>();
    for (auto p : positions)
        c->push_back ({ p, juce::MidiMessage::noteOn (1, note, (juce::uint8) 100) });
    return c;
}

std::vector<int> impulses (const juce::AudioBuffer<float>& b, float threshold = 0.5f)
{
    std::vector<int> at;
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (b.getSample (0, i) > threshold)
            at.push_back (i);
    return at;
}
} // namespace

TEST_CASE ("Generated channel: clip notes land on their transport sample, in step with the source", "[engine][synth]")
{
    AudioEngine engine;
    engine.prepareOffline (44100.0, 128);
    // the source carries a click on the right channel at transport sample 22050, like the clip's note
    auto silent = std::make_shared<juce::AudioBuffer<float>> (2, 44100);
    silent->clear();
    silent->setSample (1, 22050, 1.0f);
    REQUIRE (engine.setSource (silent));
    auto inst = std::make_shared<ImpulseInstrument>();
    REQUIRE (engine.setChannelInstrument (ChannelId::chords, inst));
    CHECK (inst->prepared);
    REQUIRE (engine.setChannelClip (ChannelId::chords, clipAt ({ 0, 1000, 1001, 22050 })));
    engine.play();
    // device blocks of odd sizes: the engine renders in fixed graph blocks either way
    juce::AudioBuffer<float> out (2, 44100), block (2, 300);
    for (int pos = 0; pos < out.getNumSamples(); pos += 300)
    {
        const int n = std::min (300, out.getNumSamples() - pos);
        engine.renderBlock (block.getArrayOfWritePointers(), 2, n);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, block, ch, 0, n);
    }
    CHECK (impulses (out) == std::vector<int> { 0, 1000, 1001, 22050 });

    SECTION ("seek moves the clip with the source and silences hanging notes")
    {
        engine.seek (21950);
        engine.play(); // the first pass played to the source's end, which stops the whole transport
        const auto after = renderEngine (engine, 512);
        // the engine serves device blocks from a fixed-size render-ahead block, so a seek takes effect
        // after the samples already rendered; what matters is that clip and source move together
        const auto clipHits = impulses (after);
        REQUIRE (clipHits.size() == 1);
        CHECK (after.getSample (1, clipHits[0]) > 0.5f); // the source's click on the same sample
        CHECK (inst->notesOff >= 1);
    }
}

TEST_CASE ("Generated channels mix in with gain; bypass mutes them and restores the mixer", "[engine][synth]")
{
    AudioEngine engine;
    engine.prepareOffline (44100.0, 128);
    auto silent = std::make_shared<juce::AudioBuffer<float>> (2, 8192);
    silent->clear();
    engine.setSource (silent);
    auto inst = std::make_shared<ImpulseInstrument>();
    engine.setChannelInstrument (ChannelId::bass, inst);
    engine.setChannelClip (ChannelId::bass, clipAt ({ 4000 }));
    engine.setChannelGainDecibels (ChannelId::bass, -6.0f);
    engine.setChannelMute (ChannelId::chords, true); // user state that bypass must give back

    SECTION ("normal: the impulse at -6 dB")
    {
        engine.play();
        const auto out = renderEngine (engine, 8192);
        CHECK_THAT (out.getSample (0, 4000), WithinAbs (juce::Decibels::decibelsToGain (-6.0f), 1e-4));
    }
    SECTION ("bypass: silent; leaving it plays the part again, chords still muted")
    {
        engine.setBypass (true);
        CHECK (engine.isBypassed());
        engine.play();
        auto out = renderEngine (engine, 8192);
        CHECK (out.getMagnitude (0, 0, 8192) == 0.0f);
        engine.setBypass (false);
        engine.seek (0);
        engine.play(); // the transport stopped at the source's end
        out = renderEngine (engine, 8192);
        CHECK_THAT (out.getSample (0, 4000), WithinAbs (juce::Decibels::decibelsToGain (-6.0f), 1e-4));
    }
}

TEST_CASE ("Built-in SFZ chords + drums through the engine graph", "[engine][synth][sfz]")
{
    AudioEngine engine;
    engine.prepareOffline (44100.0, 128);
    auto silent = std::make_shared<juce::AudioBuffer<float>> (2, 44100);
    silent->clear();
    engine.setSource (silent);
    auto pad = std::make_shared<synth::SfzInstrument>();
    REQUIRE (pad->load (*synth::findSfzPreset ("pluck")));
    engine.setChannelInstrument (ChannelId::chords, pad);
    engine.setChannelInstrument (ChannelId::drums, std::make_shared<synth::DrumInstrument>());
    auto chord = std::make_shared<MidiClip>();
    for (int n : { 60, 64, 67 })
        chord->push_back ({ 0, juce::MidiMessage::noteOn (1, n, (juce::uint8) 100) });
    for (int n : { 60, 64, 67 })
        chord->push_back ({ 20000, juce::MidiMessage::noteOff (1, n) });
    engine.setChannelClip (ChannelId::chords, chord);
    engine.setChannelClip (ChannelId::drums, clipAt ({ 0, 11025 }, 36));
    engine.play();
    const auto out = renderEngine (engine, 44100);
    CHECK (out.getMagnitude (0, 0, 4410) > 0.05f);
    CHECK (out.getMagnitude (0, 11025, 2000) > 0.05f);
}

TEST_CASE ("Generated channels perform zero allocations in realtime sections", "[engine][synth][rt]")
{
    if (! rt::isTrapInstalled())
        SKIP ("realtime trap not linked; run tools/rt_check.sh");
    AudioEngine engine;
    engine.prepareOffline (44100.0, 128);
    auto source = test::makeSine (2, 44100, 220.0, 44100.0);
    engine.setSource (source);
    auto pad = std::make_shared<synth::SfzInstrument>();
    REQUIRE (pad->load (*synth::findSfzPreset ("pad")));
    engine.setChannelInstrument (ChannelId::chords, pad);
    engine.setChannelInstrument (ChannelId::drums, std::make_shared<synth::DrumInstrument>());
    auto chords = std::make_shared<MidiClip>();
    for (juce::int64 t = 0; t < 44100; t += 5512)
        for (int n : { 60, 64, 67 })
        {
            chords->push_back ({ t, juce::MidiMessage::noteOn (1, n, (juce::uint8) 90) });
            chords->push_back ({ t + 4000, juce::MidiMessage::noteOff (1, n) });
        }
    std::stable_sort (chords->begin(), chords->end(), [] (const ClipEvent& a, const ClipEvent& b) { return a.position < b.position; });
    engine.setChannelClip (ChannelId::chords, chords);
    std::vector<juce::int64> kicks;
    for (juce::int64 t = 0; t < 44100; t += 2756)
        kicks.push_back (t);
    auto drums = std::make_shared<MidiClip>();
    for (auto t : kicks)
        drums->push_back ({ t, juce::MidiMessage::noteOn (10, 36, (juce::uint8) 110) });
    engine.setChannelClip (ChannelId::drums, drums);
    engine.play();

    juce::AudioBuffer<float> block (2, 128);
    rt::resetStats();
    for (int pos = 0; pos < 44100; pos += 128)
        engine.renderBlock (block.getArrayOfWritePointers(), 2, 128);
    CHECK (rt::getStats().allocations == 0);
    CHECK (rt::getStats().deallocations == 0);
}

TEST_CASE ("The generated parts stop with the source at its end", "[engine][synth]")
{
    AudioEngine engine;
    engine.prepareOffline (44100.0, 128);
    auto silent = std::make_shared<juce::AudioBuffer<float>> (2, 1000);
    silent->clear();
    engine.setSource (silent);
    auto inst = std::make_shared<ImpulseInstrument>();
    engine.setChannelInstrument (ChannelId::chords, inst);
    engine.setChannelClip (ChannelId::chords, clipAt ({ 500, 1500 })); // the second note is past the end
    engine.play();
    const auto out = renderEngine (engine, 4096);
    CHECK (impulses (out) == std::vector<int> { 500 });
    CHECK_FALSE (engine.getSourcePlayer().isPlaying());
}

TEST_CASE ("Bypass set before prepare: the very first samples are the source alone", "[engine][synth]")
{
    AudioEngine engine;
    engine.setBypass (true);
    engine.setChannelGainDecibels (ChannelId::source, -6.0f);
    engine.prepareOffline (44100.0, 128);
    auto source = test::makeSine (2, 4096, 440.0, 44100.0);
    engine.setSource (source);
    auto inst = std::make_shared<ImpulseInstrument>();
    engine.setChannelInstrument (ChannelId::drums, inst);
    engine.setChannelClip (ChannelId::drums, clipAt ({ 0, 10 }, 36));
    engine.play();
    const auto out = renderEngine (engine, 4096);
    const float g = juce::Decibels::decibelsToGain (-6.0f);
    for (int i = 0; i < 4096; ++i)
        REQUIRE (std::abs (out.getSample (0, i) - g * source->getSample (0, i)) < 1e-6f);
}

//==============================================================================
// fx-chain in the engine graph: per-channel inserts, keyed by the drums clip / stem, tempo-synced.
TEST_CASE ("FX: kicks from the drums clip duck another channel, even with the drums muted", "[engine][fx]")
{
    AudioEngine engine;
    engine.setChannelMute (ChannelId::drums, true); // the key comes from the player, before its strip
    engine.prepareOffline (44100.0, 128);
    engine.setSource (test::makeSine (2, 44100, 220.0, 44100.0, 0.5f));
    engine.setChannelInstrument (ChannelId::drums, std::make_shared<ImpulseInstrument>());
    engine.setChannelClip (ChannelId::drums, clipAt ({ 22050 }, 36));
    REQUIRE (engine.setFxParameter (ChannelId::source, "duck_on", "on"));
    REQUIRE (engine.setFxParameter (ChannelId::source, "duck_depth", "0.75"));
    REQUIRE_FALSE (engine.setFxParameter (ChannelId::source, "no_such_param", "1"));
    engine.play();
    const auto out = renderEngine (engine, 44100);
    auto level = [&] (int from, int to) {
        double s = 0;
        for (int i = from; i < to; ++i)
            s += (double) out.getSample (0, i) * out.getSample (0, i);
        return std::sqrt (s / (to - from));
    };
    const double before = level (18000, 22000);
    CHECK_THAT (before, WithinRel (0.5 / std::sqrt (2.0), 0.02));
    CHECK_THAT (level (22100, 22500), WithinRel (0.3 * before, 0.1)); // 1 - 0.75 x the envelope (~0.95 here: 180 ms release)
    CHECK (level (40000, 44000) > 0.85 * before);                        // released
    // a kick on another note (snare 38) does not key it
    engine.stop();
    engine.seek (0);
    engine.setChannelClip (ChannelId::drums, clipAt ({ 22050 }, 38));
    engine.play();
    const auto o2 = renderEngine (engine, 44100);
    double s = 0;
    for (int i = 22100; i < 22500; ++i)
        s += (double) o2.getSample (0, i) * o2.getSample (0, i);
    CHECK (std::sqrt (s / 400) > 0.9 * before);
}

TEST_CASE ("FX: the drums stem ducks another stem", "[engine][fx][stems]")
{
    AudioEngine engine;
    engine.setChannelMute (ChannelId::stemDrums, true);
    engine.prepareOffline (44100.0, 128);
    const int n = 44100;
    engine.setSource (test::makeSine (2, n, 220.0, 44100.0, 0.5f));
    auto stems = std::make_shared<juce::AudioBuffer<float>> (8, n);
    stems->clear();
    for (int i = 0; i < n; ++i)
        for (int ch = 0; ch < 2; ++ch)
            stems->setSample (4 + ch, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / 44100.0)); // "other"
    for (int j = 0; j < 3000; ++j)
        for (int ch = 0; ch < 2; ++ch)
            stems->setSample (ch, 22050 + j, 0.8f * std::exp ((float) -j / 600.0f) * (float) std::sin (j * 0.02)); // a kick on the drums stem
    REQUIRE (engine.setStems (stems));
    REQUIRE (engine.setFxParameter (ChannelId::stemOther, "duck_on", "on"));
    REQUIRE (engine.setFxParameter (ChannelId::stemOther, "duck_source", "Drums stem"));
    REQUIRE (engine.setFxParameter (ChannelId::stemOther, "duck_depth", "0.8"));
    engine.play();
    const auto out = renderEngine (engine, n);
    auto level = [&] (int from, int to) {
        double s = 0;
        for (int i = from; i < to; ++i)
            s += (double) out.getSample (0, i) * out.getSample (0, i);
        return std::sqrt (s / (to - from));
    };
    CHECK (level (22300, 22700) < 0.4 * level (18000, 22000));
}

TEST_CASE ("FX: the delay follows the engine's tempo", "[engine][fx]")
{
    AudioEngine engine;
    engine.prepareOffline (44100.0, 128);
    auto silent = std::make_shared<juce::AudioBuffer<float>> (2, 44100);
    silent->clear();
    engine.setSource (silent);
    engine.setChannelInstrument (ChannelId::chords, std::make_shared<ImpulseInstrument>());
    engine.setChannelClip (ChannelId::chords, clipAt ({ 2000 }));
    engine.setTempo (100.0);
    REQUIRE (engine.setFxParameter (ChannelId::chords, "delay_on", "on"));
    REQUIRE (engine.setFxParameter (ChannelId::chords, "delay_division", "1/4"));
    REQUIRE (engine.setFxParameter (ChannelId::chords, "delay_mix", "0.5"));
    engine.play();
    const auto out = renderEngine (engine, 44100);
    // the impulse (left) plus a mono-summed echo one beat later (0.6 s at 100 bpm)
    CHECK_THAT (out.getSample (0, 2000), WithinAbs (1.0, 1e-4));
    CHECK_THAT (out.getSample (0, 2000 + 26460), WithinAbs (0.25, 1e-4)); // 0.5 (mono sum) x 0.5 mix
}

TEST_CASE ("FX state round-trips through the strip (project files)", "[engine][fx]")
{
    AudioEngine engine;
    REQUIRE (engine.setFxParameter (ChannelId::bass, "filter_on", "on"));
    REQUIRE (engine.setFxParameter (ChannelId::bass, "filter_cutoff", "350"));
    const auto saved = engine.fxState (ChannelId::bass)->copyState();
    AudioEngine other;
    other.fxState (ChannelId::bass)->replaceState (saved.createCopy());
    CHECK_THAT (other.fxState (ChannelId::bass)->getRawParameterValue ("filter_cutoff")->load(), WithinRel (350.0f, 1e-3f));
    CHECK (other.fxState (ChannelId::bass)->getRawParameterValue ("filter_on")->load() == 1.0f);
    CHECK (other.fxState (ChannelId::chords)->getRawParameterValue ("filter_on")->load() == 0.0f);
}

TEST_CASE ("The graph with every FX stage on every channel performs zero allocations", "[engine][fx][rt]")
{
    if (! rt::isTrapInstalled())
        SKIP ("realtime trap not linked; run tools/rt_check.sh");
    AudioEngine engine;
    engine.prepareOffline (44100.0, 128);
    engine.setSource (test::makeSine (2, 44100, 220.0, 44100.0));
    engine.setChannelInstrument (ChannelId::drums, std::make_shared<synth::DrumInstrument>());
    auto drums = std::make_shared<MidiClip>();
    for (juce::int64 t = 0; t < 44100; t += 5512)
        drums->push_back ({ t, juce::MidiMessage::noteOn (10, 36, (juce::uint8) 110) });
    engine.setChannelClip (ChannelId::drums, drums);
    for (int c = 0; c < numChannelIds; ++c)
        for (const char* id : { "filter_on", "sat_on", "ott_on", "duck_on", "delay_on", "reverb_on" })
            REQUIRE (engine.setFxParameter ((ChannelId) c, id, "on"));
    engine.play();
    juce::AudioBuffer<float> block (2, 128);
    rt::resetStats();
    for (int pos = 0; pos < 44100; pos += 128)
    {
        if (pos % 4096 == 0) // automation while it plays (message thread, outside the section)
            for (int c = 0; c < numChannelIds; ++c)
                engine.setFxParameter ((ChannelId) c, "filter_cutoff", juce::String (200 + pos / 10));
        engine.renderBlock (block.getArrayOfWritePointers(), 2, 128);
    }
    CHECK (rt::getStats().allocations == 0);
    CHECK (rt::getStats().deallocations == 0);
}
