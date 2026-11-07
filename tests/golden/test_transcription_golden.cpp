// audio-to-midi: C++ AudioToMidi vs upstream basic-pitch 0.4.0 (ml/tests/basic_pitch_reference.py),
// and the skill acceptance (note F1 >= 0.80, onset tolerance 50 ms) on the synthetic sung melody.
// Needs build/models/basic_pitch.onnx (tools/models.py --only basic_pitch); the upstream comparisons
// also need -DJAUDIOS_ML_PYTHON. Otherwise SKIP.
#include "NpyReader.h"
#include "TranscriptionMetrics.h"

#include "engine/analysis/AudioImport.h"
#include "engine/analysis/transcription/AudioToMidi.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

using namespace jaudios;
using namespace jaudios::analysis::transcription;
using G = BasicPitchGeometry;

namespace
{
juce::File refFile (const juce::String& name) { return juce::File (JAUDIOS_BASIC_PITCH_REFERENCE_DIR).getChildFile (name); }
juce::File devModel() { return juce::File (JAUDIOS_DEV_MODELS_DIR).getChildFile ("basic_pitch.onnx"); }
juce::File fixture (const juce::String& name, const char* ext) { return juce::File (JAUDIOS_SOURCE_DIR).getChildFile ("tests/fixtures/" + name + ext); }

#define REQUIRE_MODEL() if (! devModel().existsAsFile()) SKIP ("no model: python3 tools/models.py --only basic_pitch")
#define REQUIRE_REF(n) if (! refFile (n).existsAsFile()) SKIP ("reference missing: configure with -DJAUDIOS_ML_PYTHON=<env> (ml/tests/basic_pitch_reference.py)")

AudioToMidi& model()
{
    static AudioToMidi a2m (devModel());
    return a2m;
}

juce::AudioBuffer<float> loadFixture (const juce::String& name)
{
    const analysis::AudioImporter importer;
    auto imported = importer.importFile (fixture (name, ".wav"));
    REQUIRE (imported.ok());
    return imported.capture->buffer;
}

std::vector<test::RefNote> asRef (const std::vector<Note>& notes)
{
    std::vector<test::RefNote> r;
    for (const auto& n : notes)
        r.push_back ({ n.startSec, n.endSec, n.pitch });
    return r;
}

NoteParams paramsOf (const juce::var& v)
{
    NoteParams p;
    p.onsetThreshold = v["onset_thresh"];
    p.frameThreshold = v["frame_thresh"];
    p.minNoteMs = v["min_note_ms"];
    return p;
}
} // namespace

TEST_CASE ("AudioToMidi posteriors and notes match upstream on upstream's model input", "[transcription][onnx][golden]")
{
    REQUIRE_MODEL();
    const juce::String name = GENERATE (as<juce::String>{}, "melody_mono_Emin", "stem_bass_Dmin", "pop_100_Cmaj");
    REQUIRE_REF (name + ".bp_notes.json");
    REQUIRE (model().isReady());

    const auto audio = test::readNpy (refFile (name + ".bp_audio.npy"));
    REQUIRE (audio);
    const auto pg = model().infer (audio->data.data(), (int) audio->data.size());

    struct Out { const char* key; const std::vector<float>* got; int bins; };
    for (const Out& o : { Out { "note", &pg.note, G::noteBins }, Out { "onset", &pg.onset, G::noteBins }, Out { "contour", &pg.contour, G::contourBins } })
    {
        const auto ref = test::readNpy (refFile (name + ".bp_" + o.key + ".npy"));
        REQUIRE (ref);
        REQUIRE (ref->shape[0] == (size_t) pg.frames);
        REQUIRE (ref->shape[1] == (size_t) o.bins);
        float maxDiff = 0.0f;
        for (size_t i = 0; i < ref->data.size(); ++i)
            maxDiff = std::max (maxDiff, std::abs (ref->data[i] - (*o.got)[i]));
        INFO (name << " " << o.key << ": max |C++ - upstream| = " << maxDiff);
        CHECK (maxDiff < 1e-4f); // ml-model-pipeline: basic_pitch parity 1e-4
    }

    // note creation on upstream's own posteriors must reproduce upstream's notes exactly
    Posteriorgram upstream;
    upstream.frames = pg.frames;
    upstream.note = test::readNpy (refFile (name + ".bp_note.npy"))->data;
    upstream.onset = test::readNpy (refFile (name + ".bp_onset.npy"))->data;
    upstream.contour = test::readNpy (refFile (name + ".bp_contour.npy"))->data;
    const auto expected = juce::JSON::parse (refFile (name + ".bp_notes.json"));
    for (const char* label : { "skill", "upstream" })
    {
        const auto& set = expected[label];
        const auto notes = extractNotes (upstream, paramsOf (set));
        const auto* want = set["notes"].getArray();
        REQUIRE (want != nullptr);
        INFO (name << " / " << label << ": " << notes.size() << " notes, upstream " << want->size());
        REQUIRE (notes.size() == (size_t) want->size());
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& w = (*want)[(int) i];
            INFO ("note " << i);
            REQUIRE (notes[i].start == (int) w["start"]);
            REQUIRE (notes[i].end == (int) w["end"]);
            REQUIRE (notes[i].pitch == (int) w["pitch"]);
            CHECK (std::abs (notes[i].amplitude - (double) w["amplitude"]) < 1e-5);
            CHECK (std::abs (frameToSeconds (notes[i].start) - (double) w["start_s"]) < 1e-9);
            const auto* bends = w["bends"].getArray();
            REQUIRE (bends != nullptr);
            REQUIRE (notes[i].bends.size() == (size_t) bends->size());
            for (size_t b = 0; b < notes[i].bends.size(); ++b)
                REQUIRE (notes[i].bends[b] == (int) (*bends)[(int) b]);
        }
    }
}

TEST_CASE ("AudioToMidi from the WAV (our resampler) agrees with upstream's notes", "[transcription][onnx][golden]")
{
    REQUIRE_MODEL();
    const juce::String name = GENERATE (as<juce::String>{}, "melody_mono_Emin", "stem_bass_Dmin", "pop_100_Cmaj");
    REQUIRE_REF (name + ".bp_notes.json");
    const auto r = model().transcribe (loadFixture (name), 44100.0);
    REQUIRE (r.ok());

    std::vector<test::RefNote> upstream;
    const auto ref = juce::JSON::parse (refFile (name + ".bp_notes.json")); // keep the var alive for the loop
    for (const auto& n : *ref["skill"]["notes"].getArray())
        upstream.push_back ({ (double) n["start_s"], (double) n["end_s"], (int) n["pitch"] });
    const auto s = test::noteF1 (upstream, asRef (r.notes), 0.02);
    WARN (name << ": " << r.notes.size() << " notes vs upstream " << upstream.size() << ", agreement F1 " << s.f1()
               << " (20 ms), " << r.seconds << " s");
    CHECK (s.f1() > 0.95); // only the 44.1 -> 22.05 kHz resampler differs (ours vs soxr_hq)
}

TEST_CASE ("Skill acceptance: note F1 >= 0.80 on the monophonic sung melody", "[transcription][onnx][golden][acceptance]")
{
    REQUIRE_MODEL();
    const auto truth = test::truthNotes (fixture ("melody_mono_Emin", ".truth.json"));
    REQUIRE (truth.size() == 60);
    const auto r = model().transcribe (loadFixture ("melody_mono_Emin"), 44100.0);
    REQUIRE (r.ok());
    const auto s = test::noteF1 (truth, asRef (r.notes), 0.05);
    WARN ("melody_mono_Emin (synthetic sung line): P " << s.precision() << " R " << s.recall() << " F1 " << s.f1()
          << " (" << r.notes.size() << " notes, truth " << truth.size() << ")");
    CHECK (s.f1() >= 0.80);
}

TEST_CASE ("Bass stem in monophonic mode: one note at a time, F1 vs truth", "[transcription][onnx][golden][bass]")
{
    REQUIRE_MODEL();
    const auto truth = test::truthNotes (fixture ("stem_bass_Dmin", ".truth.json"));
    REQUIRE (truth.size() == 64);
    TranscriptionSettings poly, mono;
    mono.monophonic = true;
    const auto audio = loadFixture ("stem_bass_Dmin");
    const auto rp = model().transcribe (audio, 44100.0, poly);
    const auto rm = model().transcribe (audio, 44100.0, mono);
    REQUIRE (rm.ok());
    for (size_t i = 1; i < rm.notes.size(); ++i)
        REQUIRE (rm.notes[i].startSec >= rm.notes[i - 1].endSec - 1e-9);
    const auto sp = test::noteF1 (truth, asRef (rp.notes)), sm = test::noteF1 (truth, asRef (rm.notes));
    WARN ("stem_bass_Dmin: polyphonic F1 " << sp.f1() << " (" << rp.notes.size() << " notes), monophonic F1 " << sm.f1()
          << " (" << rm.notes.size() << " notes), truth " << truth.size());
    CHECK (sm.f1() >= sp.f1());
    CHECK (sm.f1() >= 0.80);

    // MIDI: one note-on per note, every one paired with its note-off
    int ons = 0;
    for (const auto* e : rm.midi)
        if (e->message.isNoteOn())
        {
            ++ons;
            REQUIRE (e->noteOffObject != nullptr);
        }
    CHECK (ons == (int) rm.notes.size());
}
