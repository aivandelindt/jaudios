// stem-separation: C++ StemSeparator vs upstream demucs 4.1.0 PyTorch (ml/tests/demucs_reference.py).
// Needs build/models/htdemucs.onnx (tools/models.py --only demucs) and, for the references,
// -DJAUDIOS_ML_PYTHON; otherwise these tests SKIP.
#include "NpyReader.h"

#include "engine/analysis/AudioImport.h"
#include "engine/analysis/stems/DemucsSpec.h"
#include "engine/analysis/stems/StemSeparator.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <thread>

using namespace jaudios;
using namespace jaudios::analysis::stems;
using G = DemucsGeometry;

namespace
{
juce::File refFile (const char* name) { return juce::File (JAUDIOS_DEMUCS_REFERENCE_DIR).getChildFile (name); }
juce::File devModel() { return juce::File (JAUDIOS_DEV_MODELS_DIR).getChildFile ("htdemucs.onnx"); }
juce::File fixture (const char* name) { return juce::File (JAUDIOS_SOURCE_DIR).getChildFile (juce::String ("tests/fixtures/") + name + ".wav"); }

#define REQUIRE_REF(f) if (! refFile (f).existsAsFile()) SKIP ("reference missing: configure with -DJAUDIOS_ML_PYTHON=<env> (ml/tests/demucs_reference.py)")
#define REQUIRE_MODEL() if (! devModel().existsAsFile()) SKIP ("no model: python3 tools/models.py --only demucs --python <env>")

juce::AudioBuffer<float> loadFixture (const char* name, double seconds = 0.0)
{
    const analysis::AudioImporter importer;
    auto imported = importer.importFile (fixture (name));
    REQUIRE (imported.ok());
    auto b = imported.capture->buffer;
    if (seconds > 0.0)
        b.setSize (b.getNumChannels(), (int) (seconds * 44100.0), true, false, false);
    return b;
}

double db (double x) { return 20.0 * std::log10 (std::max (x, 1e-12)); }

/** max |a - b| and SNR (reference power / error power) over all stems, vs a (8, N) reference. */
std::pair<double, double> compare (const Stems& stems, const test::NpyArray& ref)
{
    const size_t n = ref.shape[1];
    double maxDiff = 0.0, sig = 0.0, err = 0.0;
    for (int s = 0; s < numStems; ++s)
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* got = stems.buffers[(size_t) s].getReadPointer (ch);
            const float* want = ref.data.data() + (size_t) (s * 2 + ch) * n;
            for (size_t i = 0; i < n; ++i)
            {
                const double d = (double) got[i] - want[i];
                maxDiff = std::max (maxDiff, std::abs (d));
                sig += (double) want[i] * want[i];
                err += d * d;
            }
        }
    return { maxDiff, 10.0 * std::log10 (sig / std::max (err, 1e-30)) };
}

/** RMS (dBFS) of sum(stems) - mix: the skill's null test. */
double nullResidualDbfs (const Stems& stems, const juce::AudioBuffer<float>& mix)
{
    double sq = 0.0;
    const int n = mix.getNumSamples();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
        {
            double sum = 0.0;
            for (const auto& b : stems.buffers)
                sum += b.getSample (ch, i);
            const double d = sum - mix.getSample (std::min (ch, mix.getNumChannels() - 1), i);
            sq += d * d;
        }
    return db (std::sqrt (sq / (2.0 * n)));
}

StemSeparator& separator()
{
    static StemSeparator sep ({ devModel() }); // 174 MB: load once per test binary
    return sep;
}
} // namespace

TEST_CASE ("DemucsSpec forward matches HTDemucs._spec + _magnitude", "[stems][stft][golden]")
{
    REQUIRE_REF ("demucs_spec.npy");
    const auto ref = test::readNpy (refFile ("demucs_spec.npy"));
    REQUIRE (ref);
    REQUIRE (ref->data.size() == (size_t) G::specSize);

    // same input as the reference: first segment of the loop, normalised by its mono reference
    auto b = loadFixture ("loop_8bar_Gmaj");
    std::vector<float> l ((size_t) G::segment), r ((size_t) G::segment);
    double sum = 0.0, sq = 0.0;
    for (int i = 0; i < G::segment; ++i)
    {
        const double m = 0.5 * ((double) b.getSample (0, i) + b.getSample (1, i));
        sum += m;
        sq += m * m;
    }
    const double mean = sum / G::segment, sd = std::sqrt ((sq - G::segment * mean * mean) / (G::segment - 1)) + 1e-8;
    for (int i = 0; i < G::segment; ++i)
    {
        l[(size_t) i] = (float) ((b.getSample (0, i) - mean) / sd);
        r[(size_t) i] = (float) ((b.getSample (1, i) - mean) / sd);
    }

    DemucsSpec spec;
    std::vector<float> z ((size_t) G::specSize);
    const float* in[] = { l.data(), r.data() };
    spec.forward (in, z.data());

    double maxDiff = 0.0, maxRef = 0.0;
    for (size_t i = 0; i < z.size(); ++i)
    {
        maxDiff = std::max (maxDiff, (double) std::abs (z[i] - ref->data[i]));
        maxRef = std::max (maxRef, (double) std::abs (ref->data[i]));
    }
    INFO ("max |C++ - torch| = " << maxDiff << " (max |value| " << maxRef << ")");
    CHECK (maxDiff < 1e-3 * std::max (1.0, maxRef));
}

TEST_CASE ("DemucsSpec inverse matches HTDemucs._mask + _ispec", "[stems][stft][golden]")
{
    REQUIRE_REF ("demucs_ispec_in.npy");
    const auto in = test::readNpy (refFile ("demucs_ispec_in.npy"));
    const auto ref = test::readNpy (refFile ("demucs_ispec_out.npy"));
    REQUIRE (in);
    REQUIRE (ref);
    REQUIRE (in->data.size() == (size_t) G::specSize);
    REQUIRE (ref->data.size() == (size_t) (2 * G::segment));

    DemucsSpec spec;
    std::vector<float> l ((size_t) G::segment, 0.0f), r ((size_t) G::segment, 0.0f);
    float* out[] = { l.data(), r.data() };
    spec.inverseAdd (in->data.data(), out);

    double maxDiff = 0.0, maxRef = 0.0;
    for (int i = 0; i < G::segment; ++i)
        for (int ch = 0; ch < 2; ++ch)
        {
            const float want = ref->data[(size_t) (ch * G::segment + i)];
            maxDiff = std::max (maxDiff, (double) std::abs (out[ch][i] - want));
            maxRef = std::max (maxRef, (double) std::abs (want));
        }
    INFO ("max |C++ - torch| = " << maxDiff << " (max |value| " << maxRef << ")");
    CHECK (maxDiff < 1e-4 * std::max (1.0, maxRef));
}

TEST_CASE ("StemSeparator matches upstream apply_model (shifts=0)", "[stems][onnx][golden]")
{
    REQUIRE_MODEL();
    struct Case { const char* ref; const char* wav; double seconds; };
    const Case cases[] = { { "demucs_loop.npy", "loop_8bar_Gmaj", 0.0 },     // 3 segments, short last chunk
                           { "demucs_short.npy", "techno_132_Fmin", 3.0 } }; // one zero-padded segment
    auto& sep = separator();
    REQUIRE (sep.isReady());

    for (const auto& c : cases)
    {
        DYNAMIC_SECTION (c.wav)
        {
            REQUIRE_REF (c.ref);
            const auto ref = test::readNpy (refFile (c.ref));
            REQUIRE (ref);
            const auto mix = loadFixture (c.wav, c.seconds);
            REQUIRE (ref->shape[0] == 8);
            REQUIRE (ref->shape[1] == (size_t) mix.getNumSamples());

            const auto r = sep.separate (mix, 44100.0);
            REQUIRE (r.stems);
            REQUIRE (r.stems->getNumSamples() == mix.getNumSamples());
            for (const auto& b : r.stems->buffers)
                REQUIRE (b.getNumChannels() == 2);

            const auto [maxDiff, snr] = compare (*r.stems, *ref);
            const double residual = nullResidualDbfs (*r.stems, mix);
            INFO (c.wav << ": " << r.segments << " segments in " << r.seconds << " s (" << sep.executionProvider() << ", "
                        << sep.modelOrigin() << "); max |C++ - PyTorch| = " << maxDiff << ", SNR " << snr
                        << " dB; null residual " << residual << " dBFS");
            WARN (c.wav << ": SNR vs PyTorch " << snr << " dB, null residual " << residual << " dBFS, "
                        << r.seconds << " s");
            CHECK (r.segments == StemSeparator::segmentCount (mix.getNumSamples()));
            // SNR 60 dB vs the reference means its SDR cannot move by more than ~0.01 dB (skill: 0.1 dB)
            CHECK (snr > 60.0);
            CHECK (maxDiff < 5e-3);
            CHECK (residual < -40.0); // skill: sum of stems vs original < -40 dBFS RMS
        }
    }
}

TEST_CASE ("StemSeparator reports progress and can be cancelled mid-inference", "[stems][onnx]")
{
    REQUIRE_MODEL();
    auto& sep = separator();
    REQUIRE (sep.isReady());
    const auto mix = loadFixture ("loop_8bar_Gmaj"); // 3 segments

    std::vector<double> seen;
    std::thread canceller;
    const auto r = sep.separate (mix, 44100.0, [&] (double p) {
        seen.push_back (p);
        if (seen.size() == 1)
            canceller = std::thread ([&] { sep.cancel(); }); // lands inside segment 2's inference
    });
    canceller.join();
    CHECK (r.cancelled);
    CHECK_FALSE (r.stems.has_value());
    CHECK (r.error.isEmpty());
    REQUIRE_FALSE (seen.empty());
    CHECK (seen.front() > 0.0);
    CHECK (seen.size() < 3);

    // the next call runs normally, with monotonic progress ending at 1
    seen.clear();
    const auto again = sep.separate (loadFixture ("techno_132_Fmin", 3.0), 44100.0, [&] (double p) { seen.push_back (p); });
    CHECK (again.stems.has_value());
    REQUIRE (seen.size() == 1);
    CHECK (seen.back() == 1.0);
}

TEST_CASE ("StemSeparator: 48 kHz mono input gives stereo stems at 48 kHz, input length", "[stems][onnx]")
{
    REQUIRE_MODEL();
    juce::AudioBuffer<float> mono (1, 48000 * 2);
    for (int i = 0; i < mono.getNumSamples(); ++i)
        mono.setSample (0, i, 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 110.0f * (float) i / 48000.0f));

    const auto r = separator().separate (mono, 48000.0);
    REQUIRE (r.stems);
    CHECK (r.stems->sampleRate == 48000.0);
    CHECK (r.stems->getNumSamples() == mono.getNumSamples());
    const double residual = nullResidualDbfs (*r.stems, mono);
    INFO ("null residual " << residual << " dBFS");
    CHECK (residual < -30.0); // resampled twice; a pure tone must still come back
}

TEST_CASE ("StemSeparator with the CoreML execution provider matches PyTorch", "[stems][onnx][coreml][golden][!mayfail]")
{
   #if ! JUCE_MAC
    SKIP ("CoreML is macOS only");
   #else
    REQUIRE_MODEL();
    REQUIRE_REF ("demucs_loop.npy");
    StemSeparator sep ({ devModel(), true });
    REQUIRE (sep.isReady());
    const auto ref = test::readNpy (refFile ("demucs_loop.npy"));
    REQUIRE (ref);
    const auto r = sep.separate (loadFixture ("loop_8bar_Gmaj"), 44100.0);
    REQUIRE (r.stems);
    const auto [maxDiff, snr] = compare (*r.stems, *ref);
    WARN ("CoreML (" << sep.executionProvider() << "): SNR vs PyTorch " << snr << " dB, max |d| " << maxDiff << ", " << r.seconds << " s");
    CHECK (snr > 60.0); // until this passes on Apple Silicon, CPU stays the default
   #endif
}
