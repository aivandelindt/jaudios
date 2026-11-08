// ui-shell: the song model behind the UI. Actions and undo / redo reach the engine (clips and
// mixer); chord replacements regenerate the bass; export tiles produce DAW-ready files.
#include "TestSignals.h"

#include "app/core/SongController.h"
#include "engine/synth/DrumSampler.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <set>

using namespace jaudios;
namespace gen = jaudios::generators;
using theory::Chord;
using theory::Quality;

namespace
{
constexpr double sr = 44100.0;

/** A synthetic analysed song: I V vi IV twice in C major, 2 s bars at 120 bpm, kicks on the beats. */
std::shared_ptr<SongAnalysis> syntheticSong()
{
    auto s = std::make_shared<SongAnalysis>();
    s->sampleRate = sr;
    s->durationSec = 16.0;
    auto& src = s->source;
    src.key = { 0, theory::Mode::major };
    src.keyMargin = 0.6;
    const std::vector<Chord> chords { { 0, Quality::maj }, { 7, Quality::maj }, { 9, Quality::min }, { 5, Quality::maj } };
    for (int b = 0; b < 8; ++b)
    {
        const auto& c = chords[(size_t) b % 4];
        const double t0 = 2.0 * b;
        gen::BarEvidence e { t0, t0 + 2.0, std::array<float, 12> {} };
        const auto pcs = c.pitchClasses();
        for (size_t k = 0; k < pcs.size(); ++k)
        {
            (*e.chroma)[(size_t) pcs[k]] = k == 0 ? 1.0f : 0.8f;
            src.melody.push_back ({ t0 + 0.5 * (double) k, t0 + 0.5 * (double) k + 0.45, 60 + pcs[k], 0.9 });
        }
        src.bass.push_back ({ t0, t0 + 1.8, 36 + c.bassPitchClass(), 0.9 });
        src.bars.push_back (e);
    }
    auto& r = s->analysis.rhythm;
    r.bpm = 120.0;
    for (int i = 0; i < 32; ++i)
    {
        r.beats.push_back (0.5 * i);
        s->kicks.push_back (0.5 * i);
        if (i % 4 == 0)
            r.downbeats.push_back (0.5 * i);
    }
    gen::EngineSettings settings;
    settings.style = gen::Style::pop;
    suggestProgressions (*s, settings);
    return s;
}

struct Fixture
{
    AudioEngine engine;
    std::shared_ptr<juce::AudioBuffer<float>> source = std::make_shared<juce::AudioBuffer<float>> (2, (int) (16.0 * sr));
    std::unique_ptr<SongController> song;
    Fixture()
    {
        source->clear();
        engine.prepareOffline (sr, 512);
        song = std::make_unique<SongController> (engine, syntheticSong(), source, gen::Style::pop);
    }
    juce::AudioBuffer<float> render (ChannelId solo, int seconds = 4)
    {
        return renderPart (engine, { "part", solo }, (juce::int64) (seconds * sr), sr, 0.0);
    }
};
} // namespace

TEST_CASE ("SongController puts the top progression's parts on the engine", "[ui][song]")
{
    Fixture f;
    REQUIRE (f.song->numProgressions() > 1);
    CHECK (f.song->progressionIndex() == 0);
    CHECK (f.song->currentProgression().chords == f.song->suggestion (0).chords);
    CHECK (f.song->parts().chords.getNumEvents() > 0);
    CHECK (f.song->parts().bassNotes.size() > 0);
    CHECK (f.song->parts().drumNotes.size() > 0);
    CHECK (f.render (ChannelId::chords).getMagnitude (0, 0, (int) (4 * sr)) > 0.01f);
    CHECK (f.render (ChannelId::bass).getMagnitude (0, 0, (int) (4 * sr)) > 0.01f);
    CHECK (f.render (ChannelId::drums).getMagnitude (0, 0, (int) (4 * sr)) > 0.01f);
    CHECK_THAT (f.song->gainDecibels (ChannelId::chords), Catch::Matchers::WithinAbs (-12.0, 1e-6)); // the CLI's mix
}

TEST_CASE ("Choosing another progression, then undo / redo", "[ui][song]")
{
    Fixture f;
    const auto first = f.song->currentProgression().chords;
    int changed = 0;
    f.song->onPartsChanged = [&] { ++changed; };
    f.song->selectProgression (1);
    CHECK (f.song->progressionIndex() == 1);
    CHECK (f.song->currentProgression().chords == f.song->suggestion (1).chords);
    CHECK (changed >= 1);
    REQUIRE (f.song->undoManager().undo());
    CHECK (f.song->progressionIndex() == 0);
    CHECK (f.song->currentProgression().chords == first);
    REQUIRE (f.song->undoManager().redo());
    CHECK (f.song->progressionIndex() == 1);
}

TEST_CASE ("Replacing a chord regenerates the bass on it; undo restores both", "[ui][song]")
{
    Fixture f;
    const int slot = 1;
    const auto alts = f.song->alternativesFor (slot);
    REQUIRE_FALSE (alts.empty());
    const auto before = f.song->currentProgression().chords[(size_t) slot];
    const auto replacement = alts.front().chord;
    REQUIRE (replacement != before);
    auto rootsIn = [&] (int s) {
        std::set<int> pcs;
        for (const auto& n : f.song->parts().bassNotes)
            if (n.slot == s && n.role == gen::BassRole::root)
                pcs.insert (n.pitch % 12);
        return pcs;
    };
    f.song->replaceChord (slot, replacement);
    CHECK (f.song->isReplaced (slot));
    CHECK (f.song->currentProgression().chords[(size_t) slot] == replacement);
    CHECK (rootsIn (slot) == std::set<int> { replacement.bassPitchClass() });
    // a second replacement of the same slot is its own undo step
    f.song->replaceChord (slot, alts.size() > 1 ? alts[1].chord : before);
    REQUIRE (f.song->undoManager().undo());
    CHECK (f.song->currentProgression().chords[(size_t) slot] == replacement);
    REQUIRE (f.song->undoManager().undo());
    CHECK_FALSE (f.song->isReplaced (slot));
    CHECK (f.song->currentProgression().chords[(size_t) slot] == before);
    CHECK (rootsIn (slot) == std::set<int> { before.bassPitchClass() });
    // choosing another progression drops replacements (undoable too)
    f.song->replaceChord (slot, replacement);
    f.song->selectProgression (2);
    CHECK_FALSE (f.song->isReplaced (slot));
    REQUIRE (f.song->undoManager().undo());
    CHECK (f.song->isReplaced (slot));
    CHECK (f.song->progressionIndex() == 0);
}

TEST_CASE ("A style change re-suggests and switches the instruments; undo goes back", "[ui][song]")
{
    Fixture f;
    const auto popTop = f.song->currentProgression().chords;
    f.song->replaceChord (0, Chord { 2, Quality::min });
    f.song->setStyle (gen::Style::techno);
    CHECK (f.song->style() == gen::Style::techno);
    CHECK (f.song->progressionIndex() == 0);
    CHECK_FALSE (f.song->isReplaced (0));
    CHECK (f.song->parts().drumPattern != nullptr);
    CHECK (f.song->parts().drumPattern->style == "techno");
    REQUIRE (f.song->undoManager().undo());
    CHECK (f.song->style() == gen::Style::pop);
    CHECK (f.song->isReplaced (0));
    CHECK (f.song->parts().drumPattern->style == "pop");
    REQUIRE (f.song->undoManager().undo());
    CHECK (f.song->currentProgression().chords == popTop);
}

TEST_CASE ("Mixer actions reach the engine and undo; a fader drag is one undo step", "[ui][song]")
{
    Fixture f;
    f.song->setMute (ChannelId::chords, true);
    CHECK (f.render (ChannelId::drums).getMagnitude (0, 0, (int) (2 * sr)) > 0.01f);
    // muted chords: silent even when soloed (renderPart solos)
    CHECK (f.render (ChannelId::chords, 2).getMagnitude (0, 0, (int) (2 * sr)) == 0.0f);
    REQUIRE (f.song->undoManager().undo());
    CHECK_FALSE (f.song->isMuted (ChannelId::chords));
    CHECK (f.render (ChannelId::chords, 2).getMagnitude (0, 0, (int) (2 * sr)) > 0.01f);

    for (float dB = -10.0f; dB <= -2.0f; dB += 1.0f) // a drag
        f.song->setGainDecibels (ChannelId::bass, dB);
    CHECK_THAT (f.song->gainDecibels (ChannelId::bass), Catch::Matchers::WithinAbs (-2.0, 1e-6));
    REQUIRE (f.song->undoManager().undo());
    CHECK_THAT (f.song->gainDecibels (ChannelId::bass), Catch::Matchers::WithinAbs (-9.0, 1e-6)); // back before the drag
    f.song->setSolo (ChannelId::drums, true);
    CHECK (f.engine.isChannelSoloed (ChannelId::drums));
    REQUIRE (f.song->undoManager().undo());
    CHECK_FALSE (f.engine.isChannelSoloed (ChannelId::drums));
}

TEST_CASE ("Export tiles: MIDI and WAV files for dragging into a DAW", "[ui][song][export]")
{
    Fixture f;
    test::TempDir tmp;
    f.song->setSongName ("Demo");
    REQUIRE (f.engine.setFxParameter (ChannelId::chords, "reverb_on", "on"));
    const auto mid = f.song->exportMidi ("chords", tmp.dir);
    REQUIRE (mid.existsAsFile());
    CHECK (mid.getFileName() == "Demo - Chords (C major, 120 BPM).mid");
    CHECK (f.song->exportMidi ("all", tmp.dir).existsAsFile());
    CHECK_FALSE (f.song->exportMidi ("nope", tmp.dir).exists());
    f.song->setMute (ChannelId::bass, true); // a single part exports whatever the mute says
    const auto wav = f.song->exportWav ("bass", tmp.dir, 1.0);
    REQUIRE (wav.existsAsFile());
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (wav));
    REQUIRE (r != nullptr);
    CHECK (r->lengthInSamples == (juce::int64) (17.0 * sr));
    juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
    r->read (&b, 0, b.getNumSamples(), 0, true, true);
    CHECK (b.getMagnitude (0, 0, b.getNumSamples()) > 0.01f);
    CHECK_FALSE (f.song->exportWav ("vocals_stem", tmp.dir).exists()); // no stems in this song
}

namespace
{
int countDrumNoteOns (const SongController& s, int note)
{
    int n = 0;
    for (const auto* e : synth::drumsToMidi (s.parts().drumNotes))
        if (e->message.isNoteOn() && e->message.getNoteNumber() == note)
            ++n;
    return n;
}
} // namespace

TEST_CASE ("Accent editor: a click cycles a drum hit accent -> ghost -> off -> groove; undo per click", "[ui][song][accents]")
{
    Fixture f;
    // the first kick of bar 2 (the pop groove has a kick on every downbeat)
    const SongController::DrumHit kick { gen::DrumElement::kick, 1, 0 };
    const int base = f.song->generatedDrumVelocity (kick);
    REQUIRE (base > 0);
    const int kicks = countDrumNoteOns (*f.song, 36);
    f.song->cycleDrumAccent (kick);
    CHECK (f.song->drumVelocity (kick) == SongController::accentOf (base));
    CHECK (f.song->isDrumHitEdited (kick));
    f.song->cycleDrumAccent (kick);
    CHECK (f.song->drumVelocity (kick) == SongController::ghostOf (base));
    f.song->cycleDrumAccent (kick);
    CHECK (f.song->drumVelocity (kick) == 0);
    CHECK (countDrumNoteOns (*f.song, 36) == kicks - 1); // gone from the clip too
    f.song->cycleDrumAccent (kick);
    CHECK_FALSE (f.song->isDrumHitEdited (kick));
    CHECK (f.song->drumVelocity (kick) == base);
    // every click was its own undo step
    for (int expected : { 0, SongController::ghostOf (base), SongController::accentOf (base), base })
    {
        REQUIRE (f.song->undoManager().undo());
        CHECK (f.song->drumVelocity (kick) == expected);
    }
}

TEST_CASE ("Accent editor: velocity drags are one undo step; an empty step gets an added hit", "[ui][song][accents]")
{
    Fixture f;
    const SongController::DrumHit hat { gen::DrumElement::hat, 2, 2 };
    const int base = f.song->generatedDrumVelocity (hat);
    REQUIRE (base > 0);
    for (int v = base; v <= base + 20; v += 4) // a drag
        f.song->setDrumVelocity (hat, v);
    CHECK (f.song->drumVelocity (hat) == base + 20);
    bool found = false;
    for (const auto& n : f.song->parts().drumNotes)
        if (n.element == hat.element && n.bar == hat.bar && n.step == hat.step)
            found = n.velocity == base + 20;
    CHECK (found);
    REQUIRE (f.song->undoManager().undo());
    CHECK (f.song->drumVelocity (hat) == base);

    // a clap where the groove has none: click adds it at the generator's position for that step
    SongController::DrumHit added { gen::DrumElement::clap, 3, 3 };
    while (f.song->generatedDrumVelocity (added) > 0)
        ++added.step;
    const int claps = countDrumNoteOns (*f.song, 39);
    f.song->cycleDrumAccent (added);
    CHECK (f.song->drumVelocity (added) == SongController::addedHitVelocity);
    CHECK (countDrumNoteOns (*f.song, 39) == claps + 1);
    const auto bars = songBars (f.song->song());
    const double stepLen = (bars[3].second - bars[3].first) / 16.0;
    bool placed = false;
    for (const auto& n : f.song->parts().drumNotes)
        if (n.element == added.element && n.bar == 3 && n.step == added.step)
            placed = std::abs (n.startSec - (bars[3].first + added.step * stepLen)) < 0.5 * stepLen;
    CHECK (placed);
    f.song->clearAccents();
    CHECK (f.song->numAccentEdits() == 0);
    CHECK (countDrumNoteOns (*f.song, 39) == claps);
}

TEST_CASE ("Accent edits survive chord and progression changes, and a style change drops them", "[ui][song][accents]")
{
    Fixture f;
    const auto& bassNotes = f.song->parts().bassNotes;
    REQUIRE (bassNotes.size() > 4);
    const auto target = bassNotes[3];
    const int step = f.song->bassStepOf (target);
    f.song->cycleBassAccent (step);
    const int accented = SongController::accentOf (target.velocity);
    CHECK (f.song->bassVelocity (step) == accented);
    const SongController::DrumHit kick { gen::DrumElement::kick, 0, 0 };
    f.song->cycleDrumAccent (kick);

    // another chord in that slot: new pitches, same rhythm, the accent stays on its step
    const auto alts = f.song->alternativesFor (target.slot);
    REQUIRE_FALSE (alts.empty());
    f.song->replaceChord (target.slot, alts.front().chord);
    bool kept = false;
    for (const auto& n : f.song->parts().bassNotes)
        if (f.song->bassStepOf (n) == step)
            kept = n.velocity == accented;
    CHECK (kept);
    f.song->selectProgression (1);
    CHECK (f.song->bassVelocity (step) == accented);
    CHECK (f.song->isDrumHitEdited (kick));

    f.song->setStyle (gen::Style::techno); // another groove: the edits no longer mean anything
    CHECK (f.song->numAccentEdits() == 0);
    REQUIRE (f.song->undoManager().undo());
    CHECK (f.song->numAccentEdits() == 2);
    // a step with no bass note cannot be "edited"
    int empty = step + 1;
    while (f.song->generatedBassVelocity (empty) > 0)
        ++empty;
    f.song->setBassVelocity (empty, 100);
    CHECK_FALSE (f.song->isBassNoteEdited (empty));
}

namespace
{
bool waitFor (const WavExportJob& job, int timeoutMs = 120000)
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (job.state() == WavExportJob::State::rendering && juce::Time::getMillisecondCounter() < until)
        juce::Thread::sleep (5);
    return job.state() != WavExportJob::State::rendering;
}
} // namespace

TEST_CASE ("WAV export jobs render on the export thread while the live engine keeps playing", "[ui][song][export]")
{
    Fixture f;
    test::TempDir a, b;
    const auto blocking = f.song->exportWav ("chords", a.dir, 0.5);
    REQUIRE (blocking.existsAsFile());

    auto job = f.song->startWavExport ("chords", b.dir, 0.5);
    REQUIRE (job != nullptr);
    CHECK (job->revision() == f.song->revision());
    // the live engine renders on this thread meanwhile (as the audio callback would)
    f.engine.play();
    juce::AudioBuffer<float> block (2, 512);
    for (int i = 0; i < 200; ++i)
        f.engine.renderBlock (block.getArrayOfWritePointers(), 2, 512);
    f.engine.stop();
    REQUIRE (waitFor (*job));
    CHECK (job->state() == WavExportJob::State::done);
    CHECK_THAT (job->progress(), Catch::Matchers::WithinAbs (1.0, 1e-9));
    REQUIRE (job->file().existsAsFile());
    // the same bytes as the blocking export: the job rendered the song as it was when it started
    juce::MemoryBlock x, y;
    REQUIRE (blocking.loadFileAsData (x));
    REQUIRE (job->file().loadFileAsData (y));
    CHECK (x == y);

    // edits after the start make a finished render out of date (the tile offers an update)
    const int before = f.song->revision();
    f.song->cycleDrumAccent ({ gen::DrumElement::kick, 0, 0 });
    CHECK (f.song->revision() != before);
    CHECK (job->revision() != f.song->revision());
    const int afterAccent = f.song->revision();
    REQUIRE (f.engine.setFxParameter (ChannelId::chords, "reverb_on", "on"));
    CHECK (f.song->revision() != afterAccent); // FX changes count too
}

TEST_CASE ("A WAV export job can be cancelled", "[ui][song][export]")
{
    Fixture f;
    test::TempDir tmp;
    auto job = f.song->startWavExport ("mix", tmp.dir, 2.0);
    REQUIRE (job != nullptr);
    job->cancel();
    REQUIRE (waitFor (*job));
    CHECK (job->state() == WavExportJob::State::cancelled);
    CHECK_FALSE (job->file().exists());
    CHECK (f.song->startWavExport ("vocals_stem", tmp.dir) == nullptr); // not exportable here
}
