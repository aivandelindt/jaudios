// stem-separation: unit tests that need neither the model nor Python.
#include "TestSignals.h"

#include "engine/analysis/stems/DemucsSpec.h"
#include "engine/analysis/stems/StemSeparator.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>
#include <juce_core/juce_core.h>

using namespace jaudios;
using namespace jaudios::analysis::stems;

TEST_CASE ("Stem names round-trip in model order", "[stems]")
{
    CHECK (std::string (stemName (Stem::drums)) == "drums");
    CHECK (std::string (stemName (Stem::vocals)) == "vocals");
    CHECK ((int) Stem::other == 2); // htdemucs model.sources = drums, bass, other, vocals
    for (auto s : allStems)
        CHECK (stemFromName (stemName (s)) == s);
    CHECK (stemFromName (" Vocals ") == Stem::vocals);
    CHECK_FALSE (stemFromName ("guitar").has_value());
}

TEST_CASE ("Segment count follows apply_model (7.8 s, 25 % overlap)", "[stems]")
{
    using G = DemucsGeometry;
    const int stride = 257985; // int(0.75 * 343980)
    CHECK (StemSeparator::segmentCount (0) == 0);
    CHECK (StemSeparator::segmentCount (1) == 1);
    CHECK (StemSeparator::segmentCount (G::segment) == 2); // range(0, length, stride): 0, stride
    CHECK (StemSeparator::segmentCount (stride) == 1);
    CHECK (StemSeparator::segmentCount (stride + 1) == 2);
    CHECK (StemSeparator::segmentCount (180 * 44100) == 31); // 3-minute track
}

TEST_CASE ("DemucsSpec: inverse(forward(x)) reconstructs the segment", "[stems][stft]")
{
    using G = DemucsGeometry;
    // Band-limited test signal: _spec drops the Nyquist bin, so white noise would not come back.
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> phase (0.0f, 6.2831853f);
    std::vector<float> l ((size_t) G::segment, 0.0f), r ((size_t) G::segment, 0.0f);
    for (int k = 1; k <= 40; ++k)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * (37.0 * k) / 44100.0; // up to 1.5 kHz
        const float pl = phase (rng), pr = phase (rng);
        for (int i = 0; i < G::segment; ++i)
        {
            l[(size_t) i] += 0.05f * (float) std::sin (w * i + pl);
            r[(size_t) i] += 0.05f * (float) std::cos (w * i + pr);
        }
    }

    DemucsSpec spec;
    std::vector<float> z ((size_t) G::specSize);
    const float* in[] = { l.data(), r.data() };
    spec.forward (in, z.data());

    std::vector<float> yl ((size_t) G::segment, 0.0f), yr ((size_t) G::segment, 0.0f);
    float* out[] = { yl.data(), yr.data() };
    spec.inverseAdd (z.data(), out);

    // _ispec re-inserts the 2 frames per side that _spec dropped as zeros (as upstream does), so
    // the first 1536 and last 1452 samples, which those frames covered, are not reconstructed.
    float maxErr = 0.0f;
    for (int i = 1536; i < 342528; ++i)
        maxErr = std::max ({ maxErr, std::abs (yl[(size_t) i] - l[(size_t) i]), std::abs (yr[(size_t) i] - r[(size_t) i]) });
    INFO ("max |istft(stft(x)) - x| = " << maxErr);
    CHECK (maxErr < 1e-3f);
}

TEST_CASE ("StemSeparator without a model reports why and does not separate", "[stems]")
{
    test::TempDir tmp;
    StemSeparator sep ({ tmp.dir.getChildFile ("htdemucs.onnx") });
    CHECK_FALSE (sep.isReady());
   #if JAUDIOS_HAS_ONNXRUNTIME
    CHECK (sep.loadError().contains ("not found"));
   #endif

    juce::AudioBuffer<float> b (2, 44100);
    b.clear();
    const auto r = sep.separate (b, 44100.0);
    CHECK_FALSE (r.stems.has_value());
    CHECK (r.error.isNotEmpty());
}

namespace
{
void writeProvenance (const juce::File& model, const juce::String& sha, double parity = 4.8e-4,
                      const juce::String& demucs = "4.1.0", juce::String ckpt = {})
{
    if (ckpt.isEmpty())
        ckpt = "8726e21a993978c7ba086d3872e7608d7d5bfca646ca4aca459ffda844faa8b4";
    auto* o = new juce::DynamicObject();
    o->setProperty ("sha256", sha);
    o->setProperty ("parity_max_abs", parity);
    o->setProperty ("parity_threshold", 1e-3);
    o->setProperty ("demucs_version", demucs);
    o->setProperty ("checkpoint_sha256", ckpt);
    o->setProperty ("platform", "darwin-arm64");
    model.getSiblingFile (model.getFileName() + ".provenance.json").replaceWithText (juce::JSON::toString (juce::var (o)));
}
} // namespace

TEST_CASE ("Demucs provenance: accepted only when it matches file, pins and parity", "[stems][provenance]")
{
    test::TempDir tmp;
    const auto model = tmp.dir.getChildFile ("htdemucs.onnx");
    model.replaceWithText ("not really a model");
    const juce::String sha = "abc123";
    juce::String why;

    SECTION ("no record")
    {
        CHECK_FALSE (verifyDemucsExport (model, sha, why));
        CHECK (why.contains ("no htdemucs.onnx.provenance.json"));
    }
    SECTION ("valid record")
    {
        writeProvenance (model, sha);
        CHECK (verifyDemucsExport (model, sha, why) == juce::String ("darwin-arm64"));
    }
    SECTION ("record for another file")
    {
        writeProvenance (model, "other");
        CHECK_FALSE (verifyDemucsExport (model, sha, why));
        CHECK (why.contains ("does not match"));
    }
    SECTION ("failing parity")
    {
        writeProvenance (model, sha, 2e-3);
        CHECK_FALSE (verifyDemucsExport (model, sha, why));
        CHECK (why.contains ("parity"));
    }
    SECTION ("other demucs version")
    {
        writeProvenance (model, sha, 4.8e-4, "4.0.1");
        CHECK_FALSE (verifyDemucsExport (model, sha, why));
        CHECK (why.contains ("demucs 4.0.1"));
    }
    SECTION ("other checkpoint")
    {
        writeProvenance (model, sha, 4.8e-4, "4.1.0", "deadbeef");
        CHECK_FALSE (verifyDemucsExport (model, sha, why));
        CHECK (why.contains ("checkpoint"));
    }
}

//==============================================================================
#include "engine/analysis/stems/StemCache.h"

namespace
{
Stems makeStems (int n, double sr = 44100.0)
{
    Stems st;
    st.sampleRate = sr;
    for (int s = 0; s < numStems; ++s)
    {
        st.buffers[(size_t) s].setSize (2, n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                st.buffers[(size_t) s].setSample (ch, i, (float) (s + 1) * 0.1f + (float) ch * 0.01f + (float) i * 1e-6f);
    }
    return st;
}
} // namespace

TEST_CASE ("Guide selection defaults to other + vocals and parses names", "[stems][guide]")
{
    const GuideSelection def;
    CHECK (def.toString() == "other+vocals");
    CHECK_FALSE (def.includes (Stem::drums));
    CHECK (GuideSelection::fromString ("bass")->toString() == "bass");
    CHECK (GuideSelection::fromString ("Vocals+bass")->toString() == "bass+vocals");
    CHECK (GuideSelection::fromString ("all")->toString() == "drums+bass+other+vocals");
    CHECK_FALSE (GuideSelection::fromString ("piano").has_value());
    CHECK_FALSE (GuideSelection::fromString ("").has_value());

    const auto st = makeStems (100);
    const auto guide = mixGuide (st);
    REQUIRE (guide.getNumChannels() == 2);
    REQUIRE (guide.getNumSamples() == 100);
    CHECK (guide.getSample (1, 50) == st[Stem::other].getSample (1, 50) + st[Stem::vocals].getSample (1, 50));
}

TEST_CASE ("packStems lays stems out as 2s+c for the engine", "[stems]")
{
    const auto st = makeStems (64);
    const auto packed = packStems (st);
    REQUIRE (packed.getNumChannels() == 8);
    REQUIRE (packed.getNumSamples() == 64);
    for (int s = 0; s < numStems; ++s)
        for (int ch = 0; ch < 2; ++ch)
            CHECK (packed.getSample (2 * s + ch, 10) == st.buffers[(size_t) s].getSample (ch, 10));
}

TEST_CASE ("Stem cache: key, exact round trip, misses and pruning", "[stems][cache]")
{
    test::TempDir tmp;
    const StemCache cache (tmp.dir.getChildFile ("stems"));

    juce::AudioBuffer<float> mix (2, 1000);
    for (int i = 0; i < 1000; ++i)
        for (int ch = 0; ch < 2; ++ch)
            mix.setSample (ch, i, std::sin (0.01f * (float) i + (float) ch));

    const auto key = StemCache::keyFor (mix, 44100.0, "model-a");
    CHECK (key.length() == 64);
    CHECK (key == StemCache::keyFor (mix, 44100.0, "model-a"));
    CHECK (key != StemCache::keyFor (mix, 48000.0, "model-a"));
    CHECK (key != StemCache::keyFor (mix, 44100.0, "model-b"));
    auto changed = mix;
    changed.setSample (1, 999, changed.getSample (1, 999) + 1e-6f);
    CHECK (key != StemCache::keyFor (changed, 44100.0, "model-a"));

    CHECK_FALSE (cache.load (key).has_value());
    const auto st = makeStems (1000, 44100.0);
    REQUIRE (cache.store (key, st));
    const auto back = cache.load (key);
    REQUIRE (back.has_value());
    CHECK (back->sampleRate == 44100.0);
    for (int s = 0; s < numStems; ++s)
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 1000; ++i)
                REQUIRE (back->buffers[(size_t) s].getSample (ch, i) == st.buffers[(size_t) s].getSample (ch, i));
    // nothing left behind but the entry
    CHECK (cache.directory().findChildFiles (juce::File::findDirectories, false).size() == 1);

    SECTION ("truncated data is a miss")
    {
        const auto data = cache.directory().getChildFile (key).getChildFile ("stems.f32");
        juce::MemoryBlock mb;
        data.loadFileAsData (mb);
        data.replaceWithData (mb.getData(), mb.getSize() - 4);
        CHECK_FALSE (cache.load (key).has_value());
    }
    SECTION ("not a key") { CHECK_FALSE (cache.load ("../etc").has_value()); }
    SECTION ("prune keeps the most recently used entry")
    {
        const auto key2 = StemCache::keyFor (changed, 44100.0, "model-a");
        REQUIRE (cache.store (key2, st));
        cache.directory().getChildFile (key).getChildFile ("meta.json").setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::hours (1));
        const auto entryBytes = (juce::int64) 1000 * 8 * 4;
        CHECK (cache.prune (entryBytes + 4096) == 1);
        CHECK_FALSE (cache.load (key).has_value());
        CHECK (cache.load (key2).has_value());
    }
}
