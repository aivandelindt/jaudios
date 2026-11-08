// packaging-and-release: the self-test the packaged app runs as its smoke test (`JAudios
// --self-test FILE`), here from the build tree: models found, ONNX key backend, scanner, analysis.
#include "TestSignals.h"

#include "app/core/SelfTest.h"
#include "engine/analysis/key/KeyDetector.h"

#include <catch2/catch_test_macros.hpp>

using namespace jaudios;

TEST_CASE ("The self-test reports models, key backend, scanner and a song's analysis", "[release]")
{
    const auto config = analysis::AppConfig::fromEnvironment();
    if (! analysis::findModelFile (config, "basic_pitch.onnx").existsAsFile() || ! analysis::findModelFile (config, "openkeyscan3.onnx").existsAsFile())
        SKIP ("models not installed (python3 tools/models.py)");
    bool ok = false;
    const auto fixture = juce::File (JAUDIOS_SOURCE_DIR).getChildFile ("tests/fixtures/loop_8bar_Gmaj.wav");
    const auto report = runSelfTest (fixture, ok);
    INFO (juce::JSON::toString (report));
    CHECK (report["models"]["openkeyscan3.onnx"].toString().endsWith ("openkeyscan3.onnx"));
    CHECK (report["models"]["basic_pitch.onnx"].toString().endsWith ("basic_pitch.onnx"));
    CHECK (report["key_backend"].toString() == "onnx");
    CHECK (report["plugin_scanner"].toString().endsWith ("jaudios-plugin-scanner"));
    CHECK (report["song"]["key"].toString().isNotEmpty());
    CHECK ((double) report["song"]["bpm"] > 100.0);
    CHECK (report["song"]["progression"].toString().isNotEmpty());
    CHECK (ok);
    CHECK ((bool) report["ok"]);

    bool missingOk = true;
    const auto bad = runSelfTest (juce::File (JAUDIOS_SOURCE_DIR).getChildFile ("no/such/file.wav"), missingOk);
    CHECK_FALSE (missingOk);
    CHECK (bad["song"]["error"].toString().isNotEmpty());
}
