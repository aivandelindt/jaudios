#include "KeyDetectorChroma.h"

#include "KeyProfiles.h"

#include "engine/analysis/AudioImport.h"
#include "engine/analysis/Resampler.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace jaudios::analysis
{

namespace
{
std::array<double, 12> zNormalise (const std::array<double, 12>& v)
{
    const double mean = std::accumulate (v.begin(), v.end(), 0.0) / 12.0;
    double var = 0.0;
    for (double x : v)
        var += (x - mean) * (x - mean);
    const double sd = std::sqrt (var / 12.0);
    std::array<double, 12> out {};
    for (size_t i = 0; i < 12; ++i)
        out[i] = sd > 0.0 ? (v[i] - mean) / sd : 0.0;
    return out;
}

std::array<double, 12> combined (const std::array<double, 12>& a, const std::array<double, 12>& b)
{
    const auto za = zNormalise (a), zb = zNormalise (b);
    std::array<double, 12> out {};
    for (size_t i = 0; i < 12; ++i)
        out[i] = 0.5 * (za[i] + zb[i]);
    return zNormalise (out);
}

const std::array<double, 12>& majorProfile()
{
    static const auto p = combined (profiles::krumhanslMajor, profiles::temperleyMajor);
    return p;
}

const std::array<double, 12>& minorProfile()
{
    static const auto p = combined (profiles::krumhanslMinor, profiles::temperleyMinor);
    return p;
}

/** Pearson correlation between z-normalised chroma (rotated so `tonic` is index 0) and profile. */
double correlation (const std::array<double, 12>& zChroma, int tonic, const std::array<double, 12>& zProfile)
{
    double sum = 0.0;
    for (int i = 0; i < 12; ++i)
        sum += zChroma[(size_t) ((i + tonic) % 12)] * zProfile[(size_t) i];
    return sum / 12.0;
}

/** Cosine between a chroma vector and the key's tonic triad template. */
double tonicTriadMatch (const Chroma12& c, Key key)
{
    const int third = key.mode == theory::Mode::major ? 4 : 3;
    double dot = 0.0, norm = 0.0;
    for (size_t i = 0; i < 12; ++i)
        norm += (double) c[i] * c[i];
    for (int interval : { 0, third, 7 })
        dot += c[(size_t) ((key.tonic + interval) % 12)];
    return norm > 0.0 ? dot / (std::sqrt (norm) * std::sqrt (3.0)) : 0.0;
}
} // namespace

Chroma12 KeyDetectorChroma::openingChroma (const ChromaFrames& frames)
{
    Chroma12 sum {};
    if (frames.frames.empty())
        return sum;

    std::vector<float> energy;
    for (const auto& f : frames.frames)
        energy.push_back (std::accumulate (f.begin(), f.end(), 0.0f));

    auto sorted = energy;
    const auto p75 = sorted.begin() + (long) (sorted.size() * 3 / 4);
    std::nth_element (sorted.begin(), p75, sorted.end());
    const float threshold = 0.3f * *p75;
    if (threshold <= 0.0f)
        return sum;

    const auto first = (size_t) (std::find_if (energy.begin(), energy.end(), [&] (float e) { return e >= threshold; }) - energy.begin());
    const auto count = (size_t) std::max (1.0, openingSec / frames.hopSec);
    for (size_t i = first; i < std::min (frames.frames.size(), first + count); ++i)
        for (size_t k = 0; k < 12; ++k)
            sum[k] += frames.frames[i][k];
    return sum;
}

KeyResult KeyDetectorChroma::scoreChroma (const Chroma12& chroma, const Chroma12& opening)
{
    KeyResult result;
    result.backend = KeyBackend::chroma;
    result.scoresAvailable = true;

    std::array<double, 12> c {};
    for (size_t i = 0; i < 12; ++i)
        c[i] = chroma[i];

    const double total = std::accumulate (c.begin(), c.end(), 0.0);
    const double spread = *std::max_element (c.begin(), c.end()) - *std::min_element (c.begin(), c.end());
    const bool noTonalContent = total <= 0.0 || spread <= 1.0e-6 * total;

    std::array<double, 24> r {};
    if (! noTonalContent)
    {
        const auto z = zNormalise (c);
        for (int k = 0; k < 24; ++k)
        {
            const auto key = theory::keyFromIndex24 (k);
            r[(size_t) k] = correlation (z, key.tonic, key.mode == theory::Mode::major ? majorProfile() : minorProfile());
        }

        // Relative major/minor (two stages, like reading a key signature then the tonic):
        //  1. the winning pitch collection (Camelot number) keeps its best profile fit;
        //  2. within that pair, mode = profile difference + opening-tonic evidence.
        // Other keys are untouched, so this can never promote an unrelated key.
        const bool hasOpening = std::any_of (opening.begin(), opening.end(), [] (float v) { return v > 0.0f; });
        if (hasOpening)
        {
            const auto winner = (int) (std::max_element (r.begin(), r.end()) - r.begin());
            const auto major = theory::keyFromIndex24 (winner).mode == theory::Mode::major ? theory::keyFromIndex24 (winner)
                                                                                              : theory::relative (theory::keyFromIndex24 (winner));
            const auto minor = theory::relative (major);
            auto& rMajor = r[(size_t) theory::index24 (major)];
            auto& rMinor = r[(size_t) theory::index24 (minor)];

            const double collection = std::max (rMajor, rMinor);
            const double modeDiff = (rMajor - rMinor)
                                  + openingTonicWeight * (tonicTriadMatch (opening, major) - tonicTriadMatch (opening, minor));
            rMajor = modeDiff >= 0.0 ? collection : collection + modeDiff;
            rMinor = modeDiff >= 0.0 ? collection - modeDiff : collection;
        }
    }

    // softmax (r / T); all-equal r (no tonal content) gives a uniform distribution
    const double maxR = *std::max_element (r.begin(), r.end());
    double sum = 0.0;
    std::array<double, 24> e {};
    for (size_t k = 0; k < 24; ++k)
        sum += (e[k] = std::exp ((r[k] - maxR) / softmaxTemperature));
    for (size_t k = 0; k < 24; ++k)
        result.scores[k] = (float) (e[k] / sum);

    std::array<int, 24> order {};
    std::iota (order.begin(), order.end(), 0);
    std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return result.scores[(size_t) a] > result.scores[(size_t) b]; });

    for (int i = 0; i < 3; ++i)
    {
        const auto key = theory::keyFromIndex24 (order[(size_t) i]);
        result.top3.push_back ({ key, theory::camelotNumber (key), theory::camelotLetter (key), result.scores[(size_t) order[(size_t) i]] });
    }

    result.margin = noTonalContent ? 0.0f : result.top3[0].score - result.top3[1].score;
    result.lowConfidence = noTonalContent || result.margin < lowConfidenceMargin;
    return result;
}

KeyResult KeyDetectorChroma::analyzeBuffer (const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    // mono downmix at the analysis rate
    juce::AudioBuffer<float> mono (1, buffer.getNumSamples());
    mono.clear();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        mono.addFrom (0, 0, buffer, ch, 0, buffer.getNumSamples(), 1.0f / (float) std::max (1, buffer.getNumChannels()));

    if (sampleRate != analysisSampleRate)
        mono = resample (mono, sampleRate, analysisSampleRate);

    return analyzeFrames (computeChroma (mono.getReadPointer (0), mono.getNumSamples(), analysisSampleRate));
}

KeyResult KeyDetectorChroma::analyzeFrames (const ChromaFrames& frames)
{
    Chroma12 global {};
    for (const auto& f : frames.frames)
        for (size_t k = 0; k < 12; ++k)
            global[k] += f[k];

    return scoreChroma (global, openingChroma (frames));
}

KeyResult KeyDetectorChroma::analyzeFile (const juce::File& file)
{
    const AudioImporter importer;
    const auto imported = importer.importFile (file);
    if (! imported.ok())
    {
        return scoreChroma ({});
    }
    return analyzeBuffer (imported.capture->buffer, imported.capture->sampleRate);
}

} // namespace jaudios::analysis
