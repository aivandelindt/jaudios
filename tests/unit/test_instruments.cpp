// synth-and-vst3-host: built-in SFZ instruments (sfizz) and the drums instrument.
#include "app/core/RealtimeTrap.h"
#include "engine/synth/SfzInstrument.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <set>

using namespace jaudios;
using namespace jaudios::synth;
using Catch::Matchers::WithinRel;

namespace
{
/** Renders `seconds` of one note (on at 0, off at `offAt`) in 256-sample blocks. */
juce::AudioBuffer<float> playNote (Instrument& inst, int note, double seconds, double offAt, double sr = 44100.0)
{
    const int total = (int) (seconds * sr), block = 256, offSample = (int) (offAt * sr);
    inst.prepare (sr, block);
    juce::AudioBuffer<float> out (2, total), tmp (2, block);
    out.clear();
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        midi.clear();
        if (pos == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
        if (offSample >= pos && offSample < pos + n)
            midi.addEvent (juce::MidiMessage::noteOff (1, note), offSample - pos);
        tmp.setSize (2, n, false, false, true);
        inst.process (tmp, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, tmp, ch, 0, n);
    }
    return out;
}

double rms (const juce::AudioBuffer<float>& b, int from, int to)
{
    double sq = 0.0;
    for (int i = from; i < to; ++i)
        sq += (double) b.getSample (0, i) * b.getSample (0, i);
    return std::sqrt (sq / std::max (1, to - from));
}

/** Fundamental from positive-going zero crossings (clean, low notes). */
double zeroCrossingHz (const juce::AudioBuffer<float>& b, int from, int to, double sr)
{
    int first = -1, last = -1, count = 0;
    for (int i = from + 1; i < to; ++i)
        if (b.getSample (0, i - 1) < 0.0f && b.getSample (0, i) >= 0.0f)
        {
            if (first < 0) first = i;
            last = i;
            ++count;
        }
    return count > 1 ? (count - 1) * sr / (last - first) : 0.0;
}
} // namespace

TEST_CASE ("Built-in SFZ presets: six, categorised, all load and sound", "[synth][sfz]")
{
    const auto& presets = builtinSfzPresets();
    REQUIRE (presets.size() == 6);
    std::set<std::string> ids;
    for (const auto& p : presets)
    {
        INFO (p.id);
        ids.insert (p.id);
        CHECK_FALSE (p.name.empty());
        CHECK_FALSE (p.category.empty());
        SfzInstrument inst;
        REQUIRE (inst.load (p));
        CHECK (inst.numRegions() >= 2);
        const auto audio = playNote (inst, p.category == "bass" ? 36 : 60, 1.5, 0.8);
        CHECK (rms (audio, 0, (int) (0.8 * 44100)) > 1e-3);
    }
    CHECK (ids == std::set<std::string> { "piano", "keys", "pad", "pluck", "sub_bass", "saw_bass" });
    CHECK (sfzCategories() == std::vector<std::string> { "keys", "pad", "pluck", "bass" });
    CHECK (findSfzPreset ("pad")->category == "pad");
    CHECK (findSfzPreset ("theremin") == nullptr);
}

TEST_CASE ("Sub bass plays the right pitch and releases", "[synth][sfz]")
{
    SfzInstrument inst;
    REQUIRE (inst.load (*findSfzPreset ("sub_bass")));
    const auto audio = playNote (inst, 36, 1.5, 0.6); // C2 = 65.41 Hz
    CHECK_THAT (zeroCrossingHz (audio, 4410, 22050, 44100.0), WithinRel (65.406, 0.01));
    CHECK (rms (audio, (int) (1.2 * 44100), (int) (1.5 * 44100)) < 1e-4); // 0.12 s release, long gone
}

TEST_CASE ("A file that is not SFZ does not load", "[synth][sfz]")
{
    SfzInstrument inst;
    CHECK_FALSE (inst.loadFile (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("missing_jaudios.sfz")));
}

TEST_CASE ("Drums instrument plays the GM kick", "[synth][drums]")
{
    DrumInstrument drums;
    const auto audio = playNote (drums, 36, 0.5, 0.1);
    CHECK (rms (audio, 0, 4410) > 1e-2);
    CHECK (drums.name().contains ("synthesised"));
}

TEST_CASE ("SFZ and drum instruments render without allocating in realtime sections", "[synth][rt]")
{
    if (! rt::isTrapInstalled())
        SKIP ("realtime trap not linked; run tools/rt_check.sh");
    SfzInstrument pad;
    REQUIRE (pad.load (*findSfzPreset ("pad")));
    pad.prepare (44100.0, 256);
    DrumInstrument drums;
    drums.prepare (44100.0, 256);
    juce::AudioBuffer<float> out (2, 256);
    juce::MidiBuffer midi;
    midi.ensureSize (1024);
    rt::resetStats();
    for (int block = 0; block < 64; ++block)
    {
        midi.clear();
        if (block % 8 == 0)
            for (int n : { 60, 64, 67 })
                midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 90), 5);
        if (block % 8 == 6)
            for (int n : { 60, 64, 67 })
                midi.addEvent (juce::MidiMessage::noteOff (1, n), 9);
        const rt::ScopedRealtimeSection rtSection;
        pad.process (out, midi);
        drums.process (out, midi);
    }
    CHECK (rt::getStats().allocations == 0);
    CHECK (rt::getStats().deallocations == 0);
}
