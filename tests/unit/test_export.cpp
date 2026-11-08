// export-and-drag-drop: the bar grid, type-1 MIDI (tempo map, key signature, track names), WAV
// formats, and parts rendered through the engine graph. Acceptance (no DAW in CI): files start
// on a bar line and their downbeats land on bar lines at the song's tempo, in MIDI ticks and in
// WAV samples, also when the music starts on a pickup.
#include "TestSignals.h"

#include "app/core/ExportService.h"
#include "app/core/Project.h"
#include "engine/export/Export.h"
#include "engine/synth/SfzInstrument.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace jaudios;
using namespace jaudios::exporting;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
constexpr double sr = 44100.0;

/** 120 bpm, first downbeat at 0.37 s (a pickup before it), 4/4. */
std::vector<double> beatsWithPickup (int count = 40, double first = 0.37 - 1.0)
{
    std::vector<double> b;
    for (int i = 0; i < count; ++i)
        b.push_back (first + 0.5 * i);
    return b;
}
std::vector<double> downbeatsOf (const std::vector<double>& beats, size_t firstIndex)
{
    std::vector<double> d;
    for (size_t i = firstIndex; i < beats.size(); i += 4)
        d.push_back (beats[i]);
    return d;
}

struct ImpulseInstrument final : synth::Instrument
{
    juce::String name() const override { return "impulse"; }
    void prepare (double, int) override {}
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi) noexcept override
    {
        out.clear();
        for (const auto m : midi)
            if (m.getMessage().isNoteOn())
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.setSample (ch, m.samplePosition, 1.0f);
    }
    void allNotesOff() noexcept override {}
};

juce::MidiFile readMidi (const juce::File& f)
{
    juce::FileInputStream in (f);
    juce::MidiFile m;
    REQUIRE (in.openedOk());
    REQUIRE (m.readFrom (in));
    return m;
}

juce::String trackName (const juce::MidiMessageSequence& t)
{
    for (const auto* e : t)
        if (e->message.isTrackNameEvent())
            return e->message.getTextFromTextMetaEvent();
    return {};
}
} // namespace

TEST_CASE ("Bar grid: beat 0 is a bar line at or before the start, prepending bars for a pickup", "[export]")
{
    const auto beats = beatsWithPickup();
    const auto grid = BarGrid::fromAnalysis (beats, downbeatsOf (beats, 2), 120.0);
    // the first downbeat (0.37 s) is bar 2's first beat; bar 1 starts 2 s earlier, before the source
    CHECK_THAT (grid.toBeats (0.37), WithinAbs (4.0, 1e-9));
    CHECK_THAT (grid.originSeconds(), WithinAbs (0.37 - 2.0, 1e-9));
    CHECK_THAT (grid.toSeconds (8.0), WithinAbs (2.37, 1e-9));
    for (double t : { -1.0, 0.0, 0.123, 5.55, 30.0 })
        CHECK_THAT (grid.toSeconds (grid.toBeats (t)), WithinAbs (t, 1e-9));

    // a song starting on its downbeat: no bar prepended
    std::vector<double> onTime;
    for (int i = 0; i < 16; ++i)
        onTime.push_back (0.5 * i);
    const auto g2 = BarGrid::fromAnalysis (onTime, downbeatsOf (onTime, 0), 120.0);
    CHECK_THAT (g2.originSeconds(), WithinAbs (0.0, 1e-12));

    // tempo drift is followed beat by beat (an accelerando from 100 to ~120 bpm)
    std::vector<double> drift { 0.0 };
    for (int i = 1; i < 32; ++i)
        drift.push_back (drift.back() + 0.6 - 0.1 * i / 31.0);
    const auto g3 = BarGrid::fromAnalysis (drift, downbeatsOf (drift, 0), 110.0);
    for (size_t i = 0; i < drift.size(); ++i)
        CHECK_THAT (g3.toBeats (drift[i]), WithinAbs ((double) i, 1e-9));
    CHECK (g3.beatLength (0) > g3.beatLength (30));

    // no beats at all: a constant grid at the given tempo
    const auto g4 = BarGrid::fromAnalysis ({}, {}, 90.0);
    CHECK_THAT (g4.toSeconds (3.0), WithinAbs (2.0, 1e-12));
}

TEST_CASE ("Key signatures on the circle of fifths", "[export]")
{
    using theory::Key;
    using theory::Mode;
    CHECK (keySignatureSharps (Key { 0, Mode::major }) == 0);  // C
    CHECK (keySignatureSharps (Key { 9, Mode::minor }) == 0);  // A minor
    CHECK (keySignatureSharps (Key { 7, Mode::major }) == 1);  // G
    CHECK (keySignatureSharps (Key { 4, Mode::minor }) == 1);  // E minor
    CHECK (keySignatureSharps (Key { 5, Mode::major }) == -1); // F
    CHECK (keySignatureSharps (Key { 2, Mode::minor }) == -1); // D minor
    CHECK (keySignatureSharps (Key { 3, Mode::major }) == -3); // Eb
    CHECK (keySignatureSharps (Key { 0, Mode::minor }) == -3); // C minor
    CHECK (keySignatureSharps (Key { 11, Mode::major }) == 5); // B
    CHECK (keySignatureSharps (Key { 6, Mode::major }) == 6);  // F#
}

TEST_CASE ("Type-1 MIDI: conductor with tempo map, 4/4 and key; named part tracks on the bar grid", "[export]")
{
    test::TempDir tmp;
    const auto beats = beatsWithPickup();
    const auto grid = BarGrid::fromAnalysis (beats, downbeatsOf (beats, 2), 120.0);
    MidiPart chords { "Chords", {} };
    for (double t : { 0.37, 2.37 }) // the first two downbeats
    {
        chords.events.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 90).withTimeStamp (t));
        chords.events.addEvent (juce::MidiMessage::noteOff (1, 57).withTimeStamp (t + 1.0));
    }
    MidiPart drums { "Drums", {} };
    drums.events.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 110).withTimeStamp (0.87)); // beat 2 of bar 2
    const auto f = tmp.dir.getChildFile ("song.mid");
    REQUIRE (writeMidiFile (f, makeMidiFile ("Song", { chords, drums }, grid, theory::Key { 9, theory::Mode::minor })));

    const auto m = readMidi (f);
    CHECK (m.getTimeFormat() == 960);
    REQUIRE (m.getNumTracks() == 3);
    const auto& conductor = *m.getTrack (0);
    CHECK (trackName (conductor) == "Song");
    bool sawTempo = false, sawTimeSig = false, sawKey = false;
    for (const auto* e : conductor)
    {
        const auto& msg = e->message;
        if (msg.isTempoMetaEvent() && e->message.getTimeStamp() == 0.0)
            sawTempo = std::abs (msg.getTempoSecondsPerQuarterNote() - 0.5) < 1e-6;
        if (msg.isTimeSignatureMetaEvent())
        {
            int num = 0, den = 0;
            msg.getTimeSignatureInfo (num, den);
            sawTimeSig = num == 4 && den == 4;
        }
        if (msg.isKeySignatureMetaEvent())
            sawKey = msg.getKeySignatureNumberOfSharpsOrFlats() == 0 && ! msg.isKeySignatureMajorKey();
    }
    CHECK (sawTempo);
    CHECK (sawTimeSig);
    CHECK (sawKey);
    CHECK (trackName (*m.getTrack (1)) == "Chords");
    CHECK (trackName (*m.getTrack (2)) == "Drums");
    std::vector<double> noteOns;
    for (const auto* e : *m.getTrack (1))
        if (e->message.isNoteOn())
            noteOns.push_back (e->message.getTimeStamp());
    CHECK (noteOns == std::vector<double> { 4 * 960.0, 8 * 960.0 }); // bars 2 and 3, exactly
    for (const auto* e : *m.getTrack (2))
        if (e->message.isNoteOn())
        {
            CHECK (e->message.getTimeStamp() == 5 * 960.0);
            CHECK (e->message.getChannel() == 10);
        }
}

TEST_CASE ("The MIDI tempo map follows a drifting beat grid", "[export]")
{
    std::vector<double> drift { 0.0 };
    for (int i = 1; i < 32; ++i)
        drift.push_back (drift.back() + 0.6 - 0.1 * i / 31.0);
    const auto grid = BarGrid::fromAnalysis (drift, downbeatsOf (drift, 0), 110.0);
    MidiPart p { "Bass", {} };
    p.events.addEvent (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 90).withTimeStamp (drift[30]));
    auto m = makeMidiFile ("Drift", { p }, grid, std::nullopt);
    m.convertTimestampTicksToSeconds(); // as a DAW honouring the tempo map would
    for (const auto* e : *m.getTrack (1))
        if (e->message.isNoteOn())
            CHECK_THAT (e->message.getTimeStamp(), WithinAbs (drift[30], 0.003)); // tempo steps are kept to 0.1 %
}

TEST_CASE ("WAV export: 24-bit and float at the source rate; alignment pads the prepended bars", "[export]")
{
    test::TempDir tmp;
    const auto sine = test::makeSine (2, 4410, 440.0, sr, 0.5f);
    for (auto format : { WavFormat::int24, WavFormat::float32 })
    {
        const auto f = tmp.dir.getChildFile (format == WavFormat::int24 ? "a24.wav" : "af.wav");
        REQUIRE (writeWav (f, *sine, sr, format));
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        REQUIRE (r != nullptr);
        CHECK (r->sampleRate == sr);
        CHECK (r->bitsPerSample == (format == WavFormat::int24 ? 24u : 32u));
        CHECK (r->usesFloatingPointData == (format == WavFormat::float32));
        juce::AudioBuffer<float> back (2, 4410);
        r->read (&back, 0, 4410, 0, true, true);
        for (int i = 0; i < 4410; i += 37)
            CHECK_THAT (back.getSample (1, i), WithinAbs (sine->getSample (1, i), format == WavFormat::int24 ? 1e-6 : 0.0));
    }
    const auto beats = beatsWithPickup();
    const auto grid = BarGrid::fromAnalysis (beats, downbeatsOf (beats, 2), 120.0);
    const auto aligned = alignToGrid (*sine, sr, grid);
    const int pad = (int) std::lround (1.63 * sr);
    CHECK (aligned.getNumSamples() == 4410 + pad);
    CHECK (aligned.getMagnitude (0, 0, pad) == 0.0f);
    CHECK (aligned.getSample (0, pad + 100) == sine->getSample (0, 100));
    CHECK (safeFileName ("Song: A/B?") == "Song AB");
}

TEST_CASE ("Exported parts land on bar lines at the song's tempo (MIDI ticks and WAV samples)", "[export][engine]")
{
    // a song with a pickup: kicks on every downbeat from 0.37 s, rendered by the engine, exported
    const auto beats = beatsWithPickup (40);
    const auto downbeats = downbeatsOf (beats, 2);
    const auto grid = BarGrid::fromAnalysis (beats, downbeats, 120.0);
    const int n = (int) (12.0 * sr);
    AudioEngine engine;
    engine.prepareOffline (sr, 512);
    auto silent = std::make_shared<juce::AudioBuffer<float>> (2, n);
    silent->clear();
    engine.setSource (silent);
    engine.setChannelInstrument (ChannelId::drums, std::make_shared<ImpulseInstrument>());
    auto clip = std::make_shared<MidiClip>();
    juce::MidiMessageSequence kicks;
    for (double d : downbeats)
        if (d * sr < n)
        {
            clip->push_back ({ (juce::int64) std::lround (d * sr), juce::MidiMessage::noteOn (10, 36, (juce::uint8) 110) });
            kicks.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 110).withTimeStamp (d));
        }
    engine.setChannelClip (ChannelId::drums, clip);

    const auto part = *findExportPart ("drums", false);
    const auto wav = alignToGrid (renderPart (engine, part, n, sr, 0.0), sr, grid);
    std::vector<int> onsets;
    for (int i = 0; i < wav.getNumSamples(); ++i)
        if (wav.getSample (0, i) > 0.5f)
            onsets.push_back (i);
    REQUIRE (onsets.size() >= 5);
    const double barSamples = 4 * 0.5 * sr;
    for (size_t k = 0; k < onsets.size(); ++k)
        CHECK (std::abs (onsets[k] - (double) (k + 1) * barSamples) <= 1.0); // bar 2, 3, ... (bar 1 = the pickup bar)

    const auto midi = makeMidiFile ("Song", { { "Drums", kicks } }, grid, std::nullopt);
    int bar = 1;
    for (const auto* e : *midi.getTrack (1))
        if (e->message.isNoteOn())
            CHECK (e->message.getTimeStamp() == (double) (bar++ * 4 * 960));
}

TEST_CASE ("Soloed part renders add up to the mix; tails ring past the end", "[export][engine]")
{
    const int n = (int) (2.0 * sr);
    AudioEngine engine;
    engine.prepareOffline (sr, 512);
    engine.setSource (test::makeSine (2, n, 110.0, sr, 0.2f));
    auto pad = std::make_shared<synth::SfzInstrument>();
    REQUIRE (pad->load (*synth::findSfzPreset ("pad")));
    engine.setChannelInstrument (ChannelId::chords, pad);
    engine.setChannelInstrument (ChannelId::drums, std::make_shared<synth::DrumInstrument>());
    auto chords = std::make_shared<MidiClip>();
    for (int note : { 57, 60, 64 })
    {
        chords->push_back ({ 1000, juce::MidiMessage::noteOn (1, note, (juce::uint8) 90) });
        chords->push_back ({ n - 4000, juce::MidiMessage::noteOff (1, note) });
    }
    std::stable_sort (chords->begin(), chords->end(), [] (const ClipEvent& a, const ClipEvent& b) { return a.position < b.position; });
    engine.setChannelClip (ChannelId::chords, chords);
    auto drums = std::make_shared<MidiClip>();
    for (juce::int64 t = 0; t < n; t += 22050)
        drums->push_back ({ t, juce::MidiMessage::noteOn (10, 36, (juce::uint8) 110) });
    engine.setChannelClip (ChannelId::drums, drums);
    REQUIRE (engine.setFxParameter (ChannelId::chords, "reverb_on", "on"));
    REQUIRE (engine.setFxParameter (ChannelId::chords, "reverb_mix", "0.5"));
    REQUIRE (engine.setFxParameter (ChannelId::source, "duck_on", "on")); // keyed by the drums, also when they are not soloed

    const double tail = 1.5;
    const auto mix = renderPart (engine, { "Mix", std::nullopt }, n, sr, tail);
    juce::AudioBuffer<float> sum (2, mix.getNumSamples());
    sum.clear();
    for (auto id : { ChannelId::source, ChannelId::chords, ChannelId::bass, ChannelId::drums })
    {
        const auto part = renderPart (engine, { "part", id }, n, sr, tail);
        REQUIRE (part.getNumSamples() == mix.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            sum.addFrom (ch, 0, part, ch, 0, part.getNumSamples());
    }
    float worst = 0.0f;
    int worstAt = -1;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < mix.getNumSamples(); ++i)
            if (std::abs (sum.getSample (ch, i) - mix.getSample (ch, i)) > worst)
            {
                worst = std::abs (sum.getSample (ch, i) - mix.getSample (ch, i));
                worstAt = i;
            }
    INFO ("worst at " << worstAt << ": sum " << sum.getSample (0, worstAt) << " mix " << mix.getSample (0, worstAt));
    CHECK (worst < 1e-4f);
    CHECK (mix.getNumSamples() == n + (int) std::lround (tail * sr));
    // the reverb tail is still there after the transport stopped at the source's end
    const auto chordsOnly = renderPart (engine, { "Chords", ChannelId::chords }, n, sr, tail);
    CHECK (chordsOnly.getMagnitude (0, n + 2000, 10000) > 1e-3f);
    // and the engine is left as it was: nothing soloed
    for (int c = 0; c < numChannelIds; ++c)
        CHECK_FALSE (engine.isChannelSoloed ((ChannelId) c));
}

TEST_CASE ("Export parts, ids and file names", "[export]")
{
    CHECK (exportableParts (false).size() == 4); // mix, chords, bass, drums
    CHECK (exportableParts (true).size() == 8);
    CHECK (findExportPart ("drums_stem", true).has_value());
    CHECK_FALSE (findExportPart ("drums_stem", false).has_value());
    CHECK (partId (*findExportPart ("Vocals stem", true)) == "vocals_stem");
    CHECK (exportFileStem ("My Song", "Chords", "A minor", 123.6) == "My Song - Chords (A minor, 124 BPM)");
    juce::File dir;
    {
        DragSession session;
        dir = session.directory();
        CHECK (dir.isDirectory());
        dir.getChildFile ("x.mid").replaceWithText ("x");
    }
    CHECK_FALSE (dir.exists());
}

TEST_CASE ("Project file v2: source, analysis, generation, instruments, FX and assets round-trip", "[export][project]")
{
    test::TempDir tmp;
    Project p;
    p.source = { "/music/song.wav", "ab12", 44100.0, 2'646'000 };
    p.analysis = { "A minor", 124.0, { 0.1, 0.584, 1.068 }, { 0.1 } };
    p.generation = { "house", "vi IV I V", { "Am", "F", "C", "G" }, "house_offbeat", "house_four_floor" };
    InstrumentSlot pad;
    pad.preset = "pad";
    p.instruments["chords"] = pad;
    AudioEngine engine;
    REQUIRE (engine.setFxParameter (ChannelId::bass, "duck_on", "on"));
    REQUIRE (engine.setFxParameter (ChannelId::bass, "duck_depth", "0.9"));
    REQUIRE (engine.setFxParameter (ChannelId::source, "filter_cutoff", "500")); // changed, but every stage off
    p.captureFx (engine);
    CHECK (p.fx.size() == 1); // only channels with a stage on
    CHECK (p.fx.count ("bass") == 1);
    p.assets = { "Song - Chords (A minor, 124 BPM).mid" };
    const auto f = tmp.dir.getChildFile ("song.jaudios.json");
    REQUIRE (p.save (f));

    juce::String error;
    const auto back = Project::load (f, error);
    INFO (error);
    REQUIRE (back.has_value());
    CHECK (back->source.sha256 == "ab12");
    CHECK (back->source.numSamples == 2'646'000);
    CHECK (back->analysis.key == "A minor");
    CHECK (back->analysis.beats == std::vector<double> { 0.1, 0.584, 1.068 });
    CHECK (back->generation.chords.joinIntoString (" ") == "Am F C G");
    CHECK (back->generation.drumPattern == "house_four_floor");
    CHECK (back->instruments.at ("chords").preset == "pad");
    CHECK (back->assets.size() == 1);
    AudioEngine fresh;
    REQUIRE (back->applyFx (fresh));
    CHECK (fresh.fxState (ChannelId::bass)->getRawParameterValue ("duck_on")->load() == 1.0f);
    CHECK_THAT (fresh.fxState (ChannelId::bass)->getRawParameterValue ("duck_depth")->load(), WithinAbs (0.9, 1e-4));
    // the instrument loader reads a version 2 file's slots, and a version 1 file still loads
    CHECK (ProjectInstruments::load (f, error).has_value());
    const auto v1 = tmp.dir.getChildFile ("old.json");
    v1.replaceWithText (R"({"version":1,"slots":{"bass":{"kind":"builtin","preset":"sub_bass"}}})");
    const auto old = Project::load (v1, error);
    REQUIRE (old.has_value());
    CHECK (old->instruments.at ("bass").preset == "sub_bass");
    CHECK (old->fx.empty());
    CHECK (channelFromName ("drums_stem") == ChannelId::stemDrums);
    CHECK (channelName (ChannelId::stemVocals) == "vocals_stem");
}
