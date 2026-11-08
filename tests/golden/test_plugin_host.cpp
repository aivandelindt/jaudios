// synth-and-vst3-host: VST3 hosting against the in-repo test instrument (tests/plugins/TestSynth):
// out-of-process scan, load, play through a generated channel, state round trip via a project file.
#include "TestSignals.h"

#include "app/core/AudioEngine.h"
#include "app/core/InstrumentSlot.h"
#include "app/core/RealtimeTrap.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace jaudios;
using Catch::Matchers::WithinRel;

namespace
{
const juce::File testPlugin() { return juce::File (JAUDIOS_TEST_VST3); }
const juce::File scannerExe() { return juce::File (JAUDIOS_PLUGIN_SCANNER); }
#define REQUIRE_TEST_PLUGIN() if (! testPlugin().exists()) SKIP ("test plugin not built: " << testPlugin().getFullPathName())

juce::PluginDescription scanTestPlugin (PluginHost& host)
{
    juce::Array<juce::PluginDescription> found;
    juce::String error;
    REQUIRE (host.scanFile (testPlugin().getFullPathName(), found, error));
    REQUIRE (found.size() == 1);
    return found.getFirst();
}

/** A C major triad for 0.5 s through `inst` (stereo, 44.1 kHz, 256-sample blocks). */
juce::AudioBuffer<float> playChord (synth::Instrument& inst, std::initializer_list<int> notes = { 60, 64, 67 }, double seconds = 0.6)
{
    inst.prepare (44100.0, 256);
    const int total = (int) (seconds * 44100.0);
    juce::AudioBuffer<float> out (2, total), block (2, 256);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += 256)
    {
        const int n = std::min (256, total - pos);
        midi.clear();
        if (pos == 0)
            for (int note : notes)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
        block.setSize (2, n, false, false, true);
        inst.process (block, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, block, ch, 0, n);
    }
    return out;
}

double zeroCrossingHz (const juce::AudioBuffer<float>& b, int from, int to)
{
    int first = -1, last = -1, count = 0;
    for (int i = from + 1; i < to; ++i)
        if (b.getSample (0, i - 1) < 0.0f && b.getSample (0, i) >= 0.0f)
        {
            if (first < 0) first = i;
            last = i;
            ++count;
        }
    return count > 1 ? (count - 1) * 44100.0 / (last - first) : 0.0;
}
} // namespace

TEST_CASE ("Out-of-process scan finds the test VST3 and survives a broken plugin", "[plugins]")
{
    REQUIRE_TEST_PLUGIN();
    PluginHost host (scannerExe());
    const auto d = scanTestPlugin (host);
    CHECK (d.name == "JAudios Test Synth");
    CHECK (d.pluginFormatName == "VST3");
    CHECK (d.isInstrument);
    CHECK (host.find ("jaudios test synth").has_value());

    // a ".vst3" that is not a plugin: the scanner process fails, the host carries on
    test::TempDir tmp;
    const auto fake = tmp.dir.getChildFile ("Broken.vst3");
    fake.getChildFile ("Contents").createDirectory();
    fake.getChildFile ("Contents/garbage.so").replaceWithText ("not a binary");
    juce::Array<juce::PluginDescription> found;
    juce::String error;
    CHECK_FALSE (host.scanFile (fake.getFullPathName(), found, error));
    CHECK (error.isNotEmpty());
    CHECK (host.known().getNumTypes() == 1);

    // a folder scan reports the broken one and still finds the good one
    juce::FileSearchPath paths;
    paths.add (tmp.dir);
    paths.add (testPlugin().getParentDirectory());
    PluginHost fresh (scannerExe());
    const auto r = fresh.scan (paths, 60000);
    CHECK (r.found.size() == 1);
    CHECK (r.failed.size() == 1);

    // the known list persists
    const auto listFile = tmp.dir.getChildFile ("plugins.xml");
    REQUIRE (fresh.saveList (listFile));
    PluginHost reloaded (scannerExe());
    REQUIRE (reloaded.loadList (listFile));
    CHECK (reloaded.find ("JAudios Test Synth").has_value());
}

TEST_CASE ("A hosted VST3 plays through a channel instrument at the right pitch", "[plugins]")
{
    REQUIRE_TEST_PLUGIN();
    PluginHost host (scannerExe());
    const auto d = scanTestPlugin (host);
    juce::String error;
    auto instance = host.create (d, 44100.0, 256, error);
    REQUIRE (instance != nullptr);
    PluginInstrument inst (std::move (instance));
    CHECK (inst.name() == "JAudios Test Synth");
    const auto audio = playChord (inst, { 69 }); // A4
    CHECK (audio.getMagnitude (0, 0, audio.getNumSamples()) > 0.05f);
    CHECK_THAT (zeroCrossingHz (audio, 2000, 20000), WithinRel (440.0, 0.01));

    // a parameter change is heard: octave +1 (normalised 0.75 of -2..2)
    REQUIRE (inst.setParameter ("Octave", 0.75f));
    inst.allNotesOff();
    const auto up = playChord (inst, { 69 });
    CHECK_THAT (zeroCrossingHz (up, 2000, 20000), WithinRel (880.0, 0.01));
}

TEST_CASE ("Plugin state persists through a project file (restart = fresh instance)", "[plugins][state]")
{
    REQUIRE_TEST_PLUGIN();
    test::TempDir tmp;
    const auto project = tmp.dir.getChildFile ("song.jaudios.json");
    juce::String before;
    {
        PluginHost host (scannerExe());
        const auto d = scanTestPlugin (host);
        juce::String error;
        auto inst = host.createInstrument (d, 44100.0, 256, error);
        REQUIRE (inst != nullptr);
        CHECK (inst->description().fileOrIdentifier == d.fileOrIdentifier); // the .vst3 bundle, not its inner binary
        REQUIRE (inst->setParameter ("Gain", 0.3f));
        REQUIRE (inst->setParameter ("Octave", 0.25f)); // -1
        before = inst->parameterValues().getDescription();

        ProjectInstruments p;
        p.slots["chords"] = *captureSlot (*inst);
        auto pad = std::make_shared<synth::SfzInstrument>();
        REQUIRE (pad->load (*synth::findSfzPreset ("pad")));
        p.slots["bass"] = *captureSlot (*pad);
        p.slots["drums"] = *captureSlot (*std::make_shared<synth::DrumInstrument>());
        REQUIRE (p.save (project));
    }
    // a new host (as after a restart) restores everything from the file alone
    PluginHost host (scannerExe());
    juce::String error;
    const auto loaded = ProjectInstruments::load (project, error);
    REQUIRE (loaded.has_value());
    REQUIRE (loaded->slots.size() == 3);
    auto chords = instantiate (loaded->slots.at ("chords"), host, 44100.0, 256, error);
    INFO (error);
    REQUIRE (chords != nullptr);
    auto* plugin = dynamic_cast<PluginInstrument*> (chords.get());
    REQUIRE (plugin != nullptr);
    CHECK (plugin->parameterValues().getDescription() == before);
    CHECK_THAT (zeroCrossingHz (playChord (*plugin, { 69 }), 2000, 20000), WithinRel (220.0, 0.01)); // octave -1
    auto bass = instantiate (loaded->slots.at ("bass"), host, 44100.0, 256, error);
    REQUIRE (bass != nullptr);
    CHECK (dynamic_cast<synth::SfzInstrument*> (bass.get())->presetId() == "pad");
    CHECK (dynamic_cast<synth::DrumInstrument*> (instantiate (loaded->slots.at ("drums"), host, 44100.0, 256, error).get()) != nullptr);
}

TEST_CASE ("A hosted VST3 on the chords channel plays generated chords in the engine graph", "[plugins][engine]")
{
    REQUIRE_TEST_PLUGIN();
    PluginHost host (scannerExe());
    const auto d = scanTestPlugin (host);
    AudioEngine engine;
    engine.prepareOffline (44100.0, 128);
    auto silent = std::make_shared<juce::AudioBuffer<float>> (2, 44100);
    silent->clear();
    engine.setSource (silent);
    juce::String error;
    REQUIRE (engine.setChannelInstrument (ChannelId::chords, host.createInstrument (d, 44100.0, 128, error)));
    auto clip = std::make_shared<MidiClip>();
    for (int n : { 60, 64, 67 })
        clip->push_back ({ 11025, juce::MidiMessage::noteOn (1, n, (juce::uint8) 100) });
    engine.setChannelClip (ChannelId::chords, clip);
    engine.play();
    juce::AudioBuffer<float> out (2, 44100), block (2, 128);
    for (int pos = 0; pos < 44100; pos += 128)
    {
        const int n = std::min (128, 44100 - pos);
        engine.renderBlock (block.getArrayOfWritePointers(), 2, n);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, block, ch, 0, n);
    }
    CHECK (out.getMagnitude (0, 0, 11025) == 0.0f);   // nothing before the chord
    CHECK (out.getMagnitude (0, 11025, 4410) > 0.05f); // the chord, on time
}

TEST_CASE ("PluginInstrument's wrapper allocates nothing around the plugin's processBlock", "[plugins][rt]")
{
    if (! rt::isTrapInstalled())
        SKIP ("realtime trap not linked; run tools/rt_check.sh");
    REQUIRE_TEST_PLUGIN();
    PluginHost host (scannerExe());
    const auto d = scanTestPlugin (host);
    juce::String error;
    PluginInstrument inst (host.create (d, 44100.0, 256, error));
    inst.prepare (44100.0, 256);
    juce::AudioBuffer<float> out (2, 256);
    juce::MidiBuffer midi;
    midi.ensureSize (1024);
    rt::resetStats();
    for (int b = 0; b < 32; ++b)
    {
        midi.clear();
        if (b % 4 == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60 + b % 12, (juce::uint8) 100), 3);
        const rt::ScopedRealtimeSection rtSection;
        inst.process (out, midi);
    }
    WARN ("allocations during hosted processBlock: " << rt::getStats().allocations);
    CHECK (rt::getStats().allocations == 0);
}
