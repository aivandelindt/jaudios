#include "Chroma.h"

#include "Stft.h"

#include <algorithm>
#include <cmath>

namespace jaudios::analysis
{

namespace
{
constexpr int fftSize = 16384;
constexpr int hop = 4096;
constexpr double minHz = 50.0, maxHz = 5000.0;
constexpr int harmonicMedianFrames = 9;   // ~0.84 s across time
constexpr int percussiveMedianBins = 17;  // ~46 Hz across frequency
constexpr float peakFloorDb = -60.0f;     // relative to frame maximum
constexpr int numHarmonics = 8;           // HPCP harmonic weighting (Gomez 2006 defaults)
constexpr double harmonicDecay = 0.6;
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

float median (std::vector<float>& v)
{
    auto mid = v.begin() + (long) (v.size() / 2);
    std::nth_element (v.begin(), mid, v.end());
    return *mid;
}

/** In-place soft HPSS mask on bins [lo, hi). */
void keepHarmonic (Spectrogram& s, int lo, int hi)
{
    const int width = hi - lo;
    std::vector<float> harmonic ((size_t) s.numFrames * (size_t) width), percussive (harmonic.size());
    std::vector<float> window;

    for (int f = 0; f < s.numFrames; ++f)
        for (int b = lo; b < hi; ++b)
        {
            window.clear();
            for (int g = std::max (0, f - harmonicMedianFrames / 2); g <= std::min (s.numFrames - 1, f + harmonicMedianFrames / 2); ++g)
                window.push_back (s.frame (g)[b]);
            harmonic[(size_t) f * (size_t) width + (size_t) (b - lo)] = median (window);

            window.clear();
            for (int c = std::max (lo, b - percussiveMedianBins / 2); c <= std::min (hi - 1, b + percussiveMedianBins / 2); ++c)
                window.push_back (s.frame (f)[c]);
            percussive[(size_t) f * (size_t) width + (size_t) (b - lo)] = median (window);
        }

    for (int f = 0; f < s.numFrames; ++f)
        for (int b = lo; b < hi; ++b)
        {
            const size_t i = (size_t) f * (size_t) width + (size_t) (b - lo);
            const float h2 = harmonic[i] * harmonic[i], p2 = percussive[i] * percussive[i];
            s.frame (f)[b] *= h2 + p2 > 0.0f ? h2 / (h2 + p2) : 0.0f;
        }
}

struct Peak
{
    double midi;   // fractional MIDI pitch
    float magnitude;
};

std::vector<std::vector<Peak>> findPeaks (const Spectrogram& s, int lo, int hi)
{
    std::vector<std::vector<Peak>> peaks ((size_t) s.numFrames);
    for (int f = 0; f < s.numFrames; ++f)
    {
        const float* m = s.frame (f);
        const float frameMax = *std::max_element (m + lo, m + hi);
        if (frameMax <= 1.0e-9f)
            continue;
        const float floor = frameMax * std::pow (10.0f, peakFloorDb / 20.0f);

        for (int b = lo + 1; b < hi - 1; ++b)
        {
            if (m[b] < floor || m[b] <= m[b - 1] || m[b] < m[b + 1])
                continue;

            // parabolic interpolation on log magnitude
            const double a = std::log (m[b - 1] + 1e-12), c = std::log (m[b] + 1e-12), d = std::log (m[b + 1] + 1e-12);
            const double denom = a - 2.0 * c + d;
            const double delta = denom < 0.0 ? 0.5 * (a - d) / denom : 0.0;
            const double hz = s.binHz (b + std::clamp (delta, -0.5, 0.5));
            peaks[(size_t) f].push_back ({ 69.0 + 12.0 * std::log2 (hz / 440.0), m[b] });
        }
    }
    return peaks;
}

/** Magnitude-weighted circular mean of each peak's deviation from the equal-tempered grid. */
double estimateTuningCents (const std::vector<std::vector<Peak>>& peaks)
{
    double re = 0.0, im = 0.0;
    for (const auto& frame : peaks)
        for (const auto& p : frame)
        {
            const double phase = kTwoPi * (p.midi - std::round (p.midi));
            re += p.magnitude * std::cos (phase);
            im += p.magnitude * std::sin (phase);
        }
    if (re == 0.0 && im == 0.0)
        return 0.0;
    return 100.0 * std::atan2 (im, re) / kTwoPi;
}
} // namespace

ChromaFrames computeChroma (const float* mono, int numSamples, double sampleRate)
{
    ChromaFrames out;
    out.hopSec = hop / sampleRate;
    out.frameSec = fftSize / sampleRate;

    if (numSamples <= 0)
        return out;

    auto spec = computeSpectrogram (mono, numSamples, sampleRate, fftSize, hop);
    const int lo = std::max (1, (int) std::floor (minHz * fftSize / sampleRate));
    const int hi = std::min (spec.numBins - 1, (int) std::ceil (maxHz * fftSize / sampleRate));

    keepHarmonic (spec, lo, hi);
    const auto peaks = findPeaks (spec, lo, hi);
    out.tuningCents = estimateTuningCents (peaks);

    out.frames.assign ((size_t) spec.numFrames, Chroma12 {});
    const double minMidi = 69.0 + 12.0 * std::log2 (minHz / 440.0);
    for (size_t f = 0; f < peaks.size(); ++f)
        for (const auto& p : peaks[f])
            for (int h = 1; h <= numHarmonics; ++h)
            {
                // HPCP harmonic weighting (Gomez 2006): a partial also counts towards the
                // fundamentals it could be the h-th harmonic of, so overtones (e.g. the fifth
                // from h = 3) reinforce their fundamental instead of inflating another class.
                const double pitch = p.midi - 12.0 * std::log2 ((double) h) - out.tuningCents / 100.0;
                if (pitch < minMidi)
                    break;
                const double nearest = std::round (pitch);
                const double distance = pitch - nearest;                              // [-0.5, 0.5] semitones
                const double weight = std::pow (std::cos (kPi * distance / 2.0), 2.0) // 1 at centre, 0.5 at +-0.5
                                    * std::pow (harmonicDecay, h - 1);
                const int pc = ((int) nearest % 12 + 12) % 12;
                out.frames[f][(size_t) pc] += (float) (weight * p.magnitude);
            }

    return out;
}

Chroma12 sumChroma (const ChromaFrames& c, double startSec, double endSec)
{
    Chroma12 sum {};
    const double half = c.frameSec * 0.5;

    auto accumulate = [&] (double margin)
    {
        int count = 0;
        for (size_t i = 0; i < c.frames.size(); ++i)
        {
            const double t = c.frameCentreSec (i);
            if (t >= startSec + margin && t < endSec - margin)
            {
                for (size_t k = 0; k < 12; ++k)
                    sum[k] += c.frames[i][k];
                ++count;
            }
        }
        return count;
    };

    if (accumulate (half) == 0)
        accumulate (0.0);

    return sum;
}

Chroma12 normalisedMax (Chroma12 c)
{
    const float m = *std::max_element (c.begin(), c.end());
    if (m > 0.0f)
        for (auto& v : c)
            v /= m;
    return c;
}

} // namespace jaudios::analysis
