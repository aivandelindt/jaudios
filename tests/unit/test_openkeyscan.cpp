#include "TestSignals.h"

#include "engine/analysis/key/KeyDetector.h"
#include "engine/analysis/openkeyscan/KeyDetectorSidecar.h"
#include "engine/analysis/openkeyscan/OpenKeyScanProtocol.h"
#include "engine/theory/Key.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

using namespace jaudios;
using namespace jaudios::analysis;
using namespace jaudios::analysis::openkeyscan;

namespace
{
juce::File fakeServer() { return juce::File (JAUDIOS_SOURCE_DIR).getChildFile ("tests/fake_openkeyscan/fake_server.py"); }

juce::File python3()
{
    for (const char* p : { "/usr/bin/python3", "/usr/local/bin/python3", "/opt/homebrew/bin/python3" })
        if (juce::File (p).existsAsFile())
            return juce::File (p);
    return {};
}

/** Sidecar config that runs the fake server through python3 with the given mode. */
SidecarConfig fakeConfig (const juce::String& mode, const test::TempDir& tmp, int fakeClass = 7)
{
    SidecarConfig c;
    c.executable = python3();
    c.arguments = { fakeServer().getFullPathName() };
    c.environment = { { "FAKE_MODE", mode },
                      { "FAKE_CLASS", juce::String (fakeClass) },
                      { "FAKE_STATE", tmp.dir.getChildFile ("starts").getFullPathName() } };
    c.readyTimeoutMs = 3000;
    c.requestTimeoutMs = 2000;
    c.cacheDir = tmp.dir.getChildFile ("cache");
    return c;
}

int processStarts (const test::TempDir& tmp)
{
    return tmp.dir.getChildFile ("starts").loadFileAsString().getIntValue();
}

juce::File writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double sr = 44100.0)
{
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr)
                                                   .withNumChannels (audio.getNumChannels()).withBitsPerSample (16));
    REQUIRE (writer != nullptr);
    writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    return file;
}

#define REQUIRE_PYTHON() if (! python3().existsAsFile()) SKIP ("python3 not found")
} // namespace

//==============================================================================
TEST_CASE ("OpenKeyScan class index maps like upstream camelot_output()", "[openkeyscan][protocol]")
{
    // Upstream: idx = class % 12 + 1, mode = "A" if class < 12 else "B"; dataset.py: 7 = A minor, 19 = C major.
    const int cls = GENERATE (range (0, 24));
    const auto key = keyFromClassIndex (cls);
    REQUIRE (key.has_value());
    CHECK (theory::camelotNumber (*key) == cls % 12 + 1);
    CHECK (theory::camelotLetter (*key) == (cls < 12 ? 'A' : 'B'));
    CHECK (classIndexFromKey (*key) == cls);

    CHECK (keyFromClassIndex (7) == theory::Key { 9, theory::Mode::minor });  // A minor
    CHECK (keyFromClassIndex (19) == theory::Key { 0, theory::Mode::major }); // C major
    CHECK (keyFromClassIndex (3) == theory::Key { 5, theory::Mode::minor });  // F minor (dataset.py)
    CHECK_FALSE (keyFromClassIndex (24).has_value());
    CHECK_FALSE (keyFromClassIndex (-1).has_value());
}

TEST_CASE ("OpenKeyScan NDJSON messages parse into typed messages", "[openkeyscan][protocol]")
{
    CHECK (parseMessage (R"({"type": "ready"})").kind == Message::Kind::ready);
    CHECK (parseMessage (R"({"type": "heartbeat"})").kind == Message::Kind::heartbeat);
    CHECK (parseMessage ("Loading model...").kind == Message::Kind::unparseable);
    CHECK (parseMessage (R"({"type": "reset_complete", "generation": 1})").kind == Message::Kind::other);

    const auto ok = parseMessage (R"({"id": "r1", "status": "success", "camelot": "8A", "openkey": "1m", "key": "A minor", "class_id": 7, "filename": "a.wav", "generation": 0})");
    REQUIRE (ok.kind == Message::Kind::success);
    CHECK (ok.id == "r1");
    CHECK (ok.key == theory::Key { 9, theory::Mode::minor });

    const auto err = parseMessage (R"({"id": "r2", "status": "error", "error": "File not found", "filename": "a.wav"})");
    REQUIRE (err.kind == Message::Kind::error);
    CHECK (err.id == "r2");
    CHECK (err.error == "File not found");

    // class_id and camelot disagree -> protocol error, not a silently wrong key
    CHECK (parseMessage (R"({"id": "r3", "status": "success", "camelot": "8B", "class_id": 7})").kind == Message::Kind::error);

    const auto req = juce::JSON::parse (makeRequest ("abc", juce::File ("/tmp/x y.wav")));
    CHECK (req["id"].toString() == "abc");
    CHECK (req["path"].toString() == "/tmp/x y.wav");
    CHECK_FALSE (makeRequest ("abc", juce::File ("/tmp/x.wav")).containsChar ('\n'));
}

TEST_CASE ("Sidecar command line follows the platform device policy", "[openkeyscan][sidecar]")
{
    const auto args = defaultServerArguments();
    CHECK (args.contains ("-w"));
    CHECK (args[args.indexOf ("-w") + 1] == "1");
    REQUIRE (args.contains ("--device"));
   #if JUCE_MAC && JUCE_ARM
    CHECK (args[args.indexOf ("--device") + 1] == "mps");
   #else
    CHECK (args[args.indexOf ("--device") + 1] == "cpu");
   #endif
}

//==============================================================================
TEST_CASE ("Sidecar returns the server's key for a file", "[openkeyscan][sidecar]")
{
    REQUIRE_PYTHON();
    test::TempDir tmp;
    const auto file = writeWav (tmp.dir.getChildFile ("song.wav"), *test::makeSine (1, 44100 * 40, 440.0, 44100.0));

    KeyDetectorSidecar detector (fakeConfig ("normal", tmp, 7));
    const auto r = detector.analyzeFile (file);

    REQUIRE (r.top3.size() == 1);
    CHECK (r.backend == KeyBackend::sidecar);
    CHECK (r.top3[0].key == theory::Key { 9, theory::Mode::minor });
    CHECK (r.top3[0].score == 1.0f);
    CHECK (r.margin == 1.0f);
    CHECK_FALSE (r.scoresAvailable); // server returns argmax only: UI hides alternatives
    CHECK (r.warning.isEmpty());
    CHECK (processStarts (tmp) == 1);

    // second request reuses the running process
    detector.analyzeFile (file);
    CHECK (processStarts (tmp) == 1);
}

TEST_CASE ("Sidecar analyzeBuffer sends a temp 44.1 kHz mono WAV and deletes it", "[openkeyscan][sidecar]")
{
    REQUIRE_PYTHON();
    test::TempDir tmp;
    KeyDetectorSidecar detector (fakeConfig ("normal", tmp, 19));

    auto stereo48k = test::makeSine (2, 48000 * 35, 440.0, 48000.0);
    const auto r = detector.analyzeBuffer (*stereo48k, 48000.0);

    REQUIRE_FALSE (r.top3.empty());
    CHECK (r.backend == KeyBackend::sidecar);
    CHECK (r.top3[0].key == theory::Key { 0, theory::Mode::major });
    CHECK (tmp.dir.getChildFile ("cache").findChildFiles (juce::File::findFiles, true, "*.wav").isEmpty());
}

TEST_CASE ("Sidecar ignores heartbeats, stray stdout and other ids", "[openkeyscan][sidecar]")
{
    REQUIRE_PYTHON();
    test::TempDir tmp;
    const auto file = writeWav (tmp.dir.getChildFile ("song.wav"), *test::makeSine (1, 44100 * 40, 440.0, 44100.0));

    KeyDetectorSidecar detector (fakeConfig ("noisy", tmp, 7));
    const auto r = detector.analyzeFile (file);
    REQUIRE_FALSE (r.top3.empty());
    CHECK (r.backend == KeyBackend::sidecar);
    CHECK (r.top3[0].key == theory::Key { 9, theory::Mode::minor });
}

TEST_CASE ("Sidecar restarts once after a crash, then succeeds", "[openkeyscan][sidecar]")
{
    REQUIRE_PYTHON();
    test::TempDir tmp;
    const auto file = writeWav (tmp.dir.getChildFile ("song.wav"), *test::makeSine (1, 44100 * 40, 440.0, 44100.0));

    KeyDetectorSidecar detector (fakeConfig ("crash_once", tmp, 7));
    const auto r = detector.analyzeFile (file);

    CHECK (r.backend == KeyBackend::sidecar);
    CHECK (processStarts (tmp) == 2);
    CHECK_FALSE (detector.hasFallenBack());
}

TEST_CASE ("Sidecar falls back to chroma after crash + failed restart", "[openkeyscan][sidecar]")
{
    REQUIRE_PYTHON();
    test::TempDir tmp;
    const auto mode = GENERATE (as<juce::String>{}, "crash_on_request", "hang_on_request", "never_ready");
    INFO (mode);

    auto audio = test::renderNotes (test::progression (7, false, { 0, 3, 4, 0 }, 1.0, 5), 40.5); // G major, 40 s
    const auto file = writeWav (tmp.dir.getChildFile ("g.wav"), audio);

    KeyDetectorSidecar detector (fakeConfig (mode, tmp, 7));
    const auto r = detector.analyzeFile (file);

    CHECK (r.backend == KeyBackend::chroma);
    CHECK (r.warning.isNotEmpty());
    CHECK (detector.hasFallenBack());
    CHECK (r.top3.at (0).key == theory::Key { 7, theory::Mode::major }); // chroma result, not the fake's 8A
    CHECK (processStarts (tmp) == 2); // original + one restart, no more

    // later requests go straight to chroma without spawning again
    detector.analyzeFile (file);
    CHECK (processStarts (tmp) == 2);
}

TEST_CASE ("Sidecar error responses fall back to chroma for that request only", "[openkeyscan][sidecar]")
{
    REQUIRE_PYTHON();
    test::TempDir tmp;
    auto audio = test::renderNotes (test::progression (7, false, { 0, 3, 4, 0 }, 1.0, 5), 40.5);
    const auto file = writeWav (tmp.dir.getChildFile ("g.wav"), audio);

    KeyDetectorSidecar detector (fakeConfig ("error", tmp, 7));
    const auto r = detector.analyzeFile (file);

    CHECK (r.backend == KeyBackend::chroma);
    CHECK (r.warning.contains ("decoder exploded"));
    CHECK_FALSE (detector.hasFallenBack()); // server is healthy; keep using it
    CHECK (processStarts (tmp) == 1);
}

TEST_CASE ("Short clips run the ensemble with chroma", "[openkeyscan][sidecar][ensemble]")
{
    REQUIRE_PYTHON();
    test::TempDir tmp;
    // 12 s G major cadence; the fake server claims 8A (A minor): they disagree.
    auto audio = test::renderNotes (test::progression (7, false, { 0, 3, 4, 0 }, 1.0, 3), 12.5);

    KeyDetectorSidecar detector (fakeConfig ("normal", tmp, 7));
    const auto r = detector.analyzeBuffer (audio, 44100.0);

    REQUIRE_FALSE (r.top3.empty());
    CHECK (r.backend == KeyBackend::sidecar);
    CHECK (r.top3[0].key == theory::Key { 9, theory::Mode::minor }); // OpenKeyScan preferred (margin 1.0 >= 0.15)
    CHECK (r.chromaAgrees == std::optional<bool> (false));
    REQUIRE (r.ensembleChroma != nullptr);
    CHECK (r.ensembleChroma->top3.at (0).key == theory::Key { 7, theory::Mode::major });
}

TEST_CASE ("makeKeyDetector builds the sidecar when an executable is configured", "[openkeyscan][factory]")
{
    REQUIRE_PYTHON();
    test::TempDir tmp;

    SECTION ("configured")
    {
        AppConfig config;
        config.keyBackend = "sidecar";
        config.sidecarExecutable = python3();
        config.sidecarArguments = { fakeServer().getFullPathName() };
        auto d = makeKeyDetector (config);
        CHECK (d.backend == KeyBackend::sidecar);
        CHECK (d.warning.isEmpty());
    }
    SECTION ("missing executable falls back to chroma with a warning")
    {
        AppConfig config;
        config.keyBackend = "sidecar";
        config.sidecarExecutable = tmp.dir.getChildFile ("does-not-exist");
        auto d = makeKeyDetector (config);
        CHECK (d.backend == KeyBackend::chroma);
        CHECK (d.warning.contains ("not found"));
    }
    SECTION ("environment")
    {
        const auto c = AppConfig::fromEnvironment ({ { "KEY_BACKEND", "sidecar" },
                                                     { "OPENKEYSCAN_SIDECAR", "/opt/oks/openkeyscan-analyzer" },
                                                     { "OPENKEYSCAN_SIDECAR_ARGS", "server.py --flag" } });
        CHECK (c.keyBackend == "sidecar");
        CHECK (c.sidecarExecutable.getFullPathName() == "/opt/oks/openkeyscan-analyzer");
        CHECK (c.sidecarArguments == juce::StringArray { "server.py", "--flag" });
    }
}
