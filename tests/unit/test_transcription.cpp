// audio-to-midi: unit tests on hand-made posteriorgrams (no model, no Python).
#include "TestSignals.h"
#include "TranscriptionMetrics.h"

#include "engine/analysis/transcription/AudioToMidi.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace jaudios;
using namespace jaudios::analysis::transcription;
using Catch::Matchers::WithinAbs;
using G = BasicPitchGeometry;

namespace
{
Posteriorgram blank (int frames)
{
    Posteriorgram pg;
    pg.frames = frames;
    pg.note.assign ((size_t) frames * G::noteBins, 0.0f);
    pg.onset.assign ((size_t) frames * G::noteBins, 0.0f);
    pg.contour.assign ((size_t) frames * G::contourBins, 0.0f);
    return pg;
}

void paint (Posteriorgram& pg, int bin, int from, int to, float level, bool onset, int contourOffset = 0)
{
    for (int t = from; t < to; ++t)
    {
        pg.note[(size_t) t * G::noteBins + (size_t) bin] = level;
        pg.contour[(size_t) t * G::contourBins + (size_t) (3 * bin + contourOffset)] = 0.9f;
    }
    if (onset)
        pg.onset[(size_t) from * G::noteBins + (size_t) bin] = 0.9f;
}
} // namespace

TEST_CASE ("Basic Pitch geometry and timing match upstream constants", "[transcription]")
{
    CHECK (G::framesPerSecond == 86);
    CHECK (G::windowSamples == 43844);
    CHECK (G::hopSamples == 36164);
    CHECK (NoteParams {}.minNoteFrames() == 5);                     // skill: 58 ms
    NoteParams upstream;
    upstream.frameThreshold = 0.3;
    upstream.minNoteMs = 127.70;
    CHECK (upstream.minNoteFrames() == 11); // upstream predict() default
    CHECK (frameToSeconds (0) == 0.0);
    CHECK_THAT (frameToSeconds (86), WithinAbs (86 * 256.0 / 22050.0, 1e-12));
    // from frame 172 on, upstream subtracts its per-window offset (256/22050 * 0.734375 + 0.0018)
    CHECK_THAT (frameToSeconds (172), WithinAbs (172 * 256.0 / 22050.0 - (256.0 / 22050.0 * 0.734375 + 0.0018), 1e-12));
}

TEST_CASE ("extractNotes: an onset peak starts a note that ends after the energy tolerance", "[transcription]")
{
    auto pg = blank (100);
    paint (pg, 40, 10, 31, 0.9f, true, 1); // bin 40 = MIDI 61; contour peak 1/3 semitone sharp

    const auto notes = extractNotes (pg);
    REQUIRE (notes.size() == 1);
    CHECK (notes[0].start == 10);
    CHECK (notes[0].end == 31);
    CHECK (notes[0].pitch == 61);
    CHECK_THAT (notes[0].amplitude, WithinAbs (0.9, 1e-6));
    REQUIRE (notes[0].bends.size() == 21);
    CHECK (notes[0].bends.front() == 1); // contour peak one bin above the note's centre
}

TEST_CASE ("extractNotes: short notes are dropped; melodia trick finds onset-less notes", "[transcription]")
{
    auto pg = blank (200);
    paint (pg, 30, 10, 14, 0.9f, true);   // 4 frames: too short (<= 5)
    paint (pg, 50, 60, 120, 0.8f, false); // no onset at all, but sustained energy

    NoteParams p;
    p.inferOnsets = false; // only model onsets: the bin-30 blip has one, bin 50 has none
    SECTION ("melodia on")
    {
        const auto notes = extractNotes (pg, p);
        REQUIRE (notes.size() == 1);
        CHECK (notes[0].pitch == 71);
        CHECK (notes[0].start == 60);
        CHECK (notes[0].end == 119); // upstream's melodia pass ends one frame earlier (i_end = i - 1 - k)
    }
    SECTION ("melodia off")
    {
        p.melodiaTrick = false;
        CHECK (extractNotes (pg, p).empty());
    }
}

TEST_CASE ("extractNotes: frequency limits remove notes outside the range", "[transcription]")
{
    auto pg = blank (100);
    paint (pg, 10, 10, 40, 0.9f, true); // MIDI 31 (49 Hz)
    paint (pg, 60, 10, 40, 0.9f, true); // MIDI 81 (880 Hz)
    NoteParams p;
    p.minFrequencyHz = 100.0;
    const auto notes = extractNotes (pg, p);
    REQUIRE (notes.size() == 1);
    CHECK (notes[0].pitch == 81);
}

TEST_CASE ("MIDI sequence: velocity, note pairs, bends dropped on overlapping notes", "[transcription][midi]")
{
    std::vector<Note> notes {
        { 0.0, 1.0, 60, 0.5f, 64, { 0, 3, 3 } },
        { 0.5, 1.5, 64, 1.0f, 127, { 1, 1 } },   // overlaps the first: both lose their bends
        { 2.0, 3.0, 67, 0.8f, 102, { 0, 6, 9 } }, // alone: keeps them; 9 * 4096 / 3 = 12288 -> clamped to 8191
    };
    const auto seq = toMidiSequence (notes);
    int noteOns = 0, wheels = 0, maxWheel = 0;
    for (const auto* e : seq)
    {
        const auto& m = e->message;
        if (m.isNoteOn())
        {
            ++noteOns;
            REQUIRE (e->noteOffObject != nullptr);
            if (m.getNoteNumber() == 60) CHECK (m.getVelocity() == 64);
        }
        if (m.isPitchWheel())
        {
            ++wheels;
            CHECK (m.getTimeStamp() >= 2.0);
            maxWheel = std::max (maxWheel, m.getPitchWheelValue());
        }
    }
    CHECK (noteOns == 3);
    CHECK (wheels == 4);              // 3 bends + re-centre
    CHECK (maxWheel == 8191 + 8192);  // clamped to +2 semitones
}

TEST_CASE ("makeMonophonic keeps the strongest note where notes overlap", "[transcription][bass]")
{
    auto note = [] (double s, double e, int p, float c) { Note n; n.startSec = s; n.endSec = e; n.pitch = p; n.confidence = c; return n; };
    std::vector<Note> notes {
        note (0.0, 1.0, 38, 0.9f),   // strong root
        note (0.1, 0.9, 50, 0.4f),   // octave ghost inside it: dropped
        note (0.8, 1.6, 45, 0.7f),   // overlaps the end: trimmed to start at 1.0
        note (2.0, 2.02, 40, 0.9f),  // standalone (short, but not overlapped): kept
    };
    const auto mono = makeMonophonic (notes);
    REQUIRE (mono.size() == 3);
    CHECK (mono[0].pitch == 38);
    CHECK (mono[1].pitch == 45);
    CHECK (mono[1].startSec == 1.0);
    CHECK (mono[2].pitch == 40);
    for (size_t i = 1; i < mono.size(); ++i)
        CHECK (mono[i].startSec >= mono[i - 1].endSec);
}

TEST_CASE ("Note F1 helper matches mir_eval semantics", "[transcription][metrics]")
{
    const std::vector<test::RefNote> ref { { 0.0, 0.5, 60 }, { 0.5, 1.0, 62 }, { 1.0, 1.5, 64 } };
    const std::vector<test::RefNote> est { { 0.04, 0.4, 60 }, { 0.56, 1.0, 62 }, { 1.0, 1.2, 65 }, { 1.01, 1.4, 64 } };
    const auto s = test::noteF1 (ref, est);
    CHECK (s.matched == 2); // 62 is 60 ms late (> 50 ms); 65 is the wrong pitch
    CHECK_THAT (s.f1(), WithinAbs (2.0 * (2.0 / 4) * (2.0 / 3) / (2.0 / 4 + 2.0 / 3), 1e-12));
}

TEST_CASE ("AudioToMidi without a model reports why", "[transcription]")
{
    test::TempDir tmp;
    AudioToMidi a2m (tmp.dir.getChildFile ("basic_pitch.onnx"));
    CHECK_FALSE (a2m.isReady());
    juce::AudioBuffer<float> b (1, 22050);
    b.clear();
    CHECK_FALSE (a2m.transcribe (b, 22050.0).ok());
}
