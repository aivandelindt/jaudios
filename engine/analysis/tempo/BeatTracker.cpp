#include "BeatTracker.h"

#include "engine/analysis/Stft.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace jaudios::analysis
{

namespace
{
constexpr double minBpm = 60.0, maxBpm = 200.0;
constexpr double priorCentreBpm = 120.0, priorOctaves = 1.0;
constexpr int combHarmonics = 4;
constexpr double dpTightness = 100.0;
constexpr double refineRadiusSec = 0.05;
constexpr double silenceDb = -40.0; // leading/trailing beats quieter than this vs. track RMS are dropped

//==============================================================================
struct OnsetEnvelope
{
    double fps = 0.0;
    std::vector<double> values; // detrended, half-wave rectified, unit variance
};

/** Log mel-band spectral flux (frames centred on t = i / fps). Pooling into mel bands gives each
    frequency region equal say, so broadband hats cannot outvote a kick that lives in a few bins. */
OnsetEnvelope onsetEnvelope (const float* x, int n, double sr)
{
    OnsetEnvelope env;
    const int hop = (int) std::lround (sr / 100.0);
    env.fps = sr / hop;

    const auto spec = computeSpectrogram (x, n, sr, 2048, hop);
    const float scale = 4.0f / (float) spec.fftSize; // ~sinusoid amplitude

    // band b covers bins [edges[b], edges[b+1])
    constexpr int numBands = 40;
    auto hzToMel = [] (double hz) { return 2595.0 * std::log10 (1.0 + hz / 700.0); };
    auto melToHz = [] (double mel) { return 700.0 * (std::pow (10.0, mel / 2595.0) - 1.0); };
    const double melLo = hzToMel (30.0), melHi = hzToMel (std::min (16000.0, sr * 0.45));
    std::vector<int> edges;
    for (int b = 0; b <= numBands; ++b)
    {
        const int bin = (int) std::lround (melToHz (melLo + (melHi - melLo) * b / numBands) * spec.fftSize / sr);
        edges.push_back (std::clamp (std::max (bin, edges.empty() ? 1 : edges.back() + 1), 1, spec.numBins - 1));
    }

    env.values.assign ((size_t) spec.numFrames, 0.0);
    std::vector<double> prev ((size_t) numBands, 0.0), cur ((size_t) numBands);
    for (int f = 0; f < spec.numFrames; ++f)
    {
        const float* m = spec.frame (f);
        double flux = 0.0;
        for (int b = 0; b < numBands; ++b)
        {
            double e = 0.0;
            for (int k = edges[(size_t) b]; k < edges[(size_t) b + 1]; ++k)
                e += (double) m[k] * m[k];
            cur[(size_t) b] = std::log1p (100.0 * scale * std::sqrt (e));
            flux += std::max (0.0, cur[(size_t) b] - prev[(size_t) b]);
        }
        env.values[(size_t) f] = f == 0 ? 0.0 : flux;
        std::swap (prev, cur);
    }

    // subtract a ~1 s moving average, rectify, normalise
    const int half = (int) env.fps / 2;
    std::vector<double> prefix (env.values.size() + 1, 0.0);
    for (size_t i = 0; i < env.values.size(); ++i)
        prefix[i + 1] = prefix[i] + env.values[i];

    std::vector<double> out (env.values.size());
    for (int i = 0; i < (int) env.values.size(); ++i)
    {
        const int a = std::max (0, i - half), b = std::min ((int) env.values.size(), i + half + 1);
        out[(size_t) i] = std::max (0.0, env.values[(size_t) i] - (prefix[(size_t) b] - prefix[(size_t) a]) / (b - a));
    }

    double sq = 0.0;
    for (double v : out)
        sq += v * v;
    const double sd = std::sqrt (sq / (double) std::max<size_t> (1, out.size()));
    if (sd > 0.0)
        for (auto& v : out)
            v /= sd;

    env.values = std::move (out);
    return env;
}

//==============================================================================
struct TempoEstimate
{
    double bpm = 0.0, confidence = 0.0;
};

TempoEstimate estimateTempo (const OnsetEnvelope& env)
{
    const auto& o = env.values;
    const int n = (int) o.size();
    const int maxLag = std::min (n - 1, (int) std::ceil (combHarmonics * 60.0 * env.fps / minBpm) + 2);
    if (maxLag < 4)
        return {};

    std::vector<double> r ((size_t) maxLag + 1, 0.0);
    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double s = 0.0;
        for (int i = 0; i + lag < n; ++i)
            s += o[(size_t) i] * o[(size_t) (i + lag)];
        r[(size_t) lag] = s;
    }
    if (r[0] <= 0.0)
        return {};

    auto acfAt = [&] (double lag)
    {
        const int i = (int) lag;
        if (i + 1 > maxLag) return 0.0;
        const double f = lag - i;
        return r[(size_t) i] * (1.0 - f) + r[(size_t) i + 1] * f;
    };

    TempoEstimate best;
    double sum = 0.0;
    int count = 0;
    for (double bpm = minBpm; bpm <= maxBpm + 1e-9; bpm += 0.02)
    {
        const double period = 60.0 * env.fps / bpm;
        double comb = 0.0;
        for (int k = 1; k <= combHarmonics; ++k)
            comb += acfAt (k * period);
        const double octaves = std::log2 (bpm / priorCentreBpm) / priorOctaves;
        const double score = comb / r[0] * std::exp (-0.5 * octaves * octaves);
        sum += score;
        ++count;
        if (score > best.confidence)
            best = { bpm, score };
    }

    const double mean = sum / count;
    best.confidence = mean > 0.0 ? best.confidence / mean : 0.0;
    return best;
}

//==============================================================================
/** Ellis (2007) dynamic-programming beat tracker. Returns beat frame indices. */
std::vector<int> trackBeats (const OnsetEnvelope& env, double bpm)
{
    const auto& o = env.values;
    const int n = (int) o.size();
    const double period = 60.0 * env.fps / bpm;
    const int minGap = std::max (1, (int) std::round (period / 2.0));
    const int maxGap = (int) std::round (2.0 * period);

    std::vector<double> score ((size_t) n, 0.0);
    std::vector<int> back ((size_t) n, -1);

    for (int t = 0; t < n; ++t)
    {
        double best = 0.0; // chains may (re)start anywhere at zero cost
        int bestPrev = -1;
        for (int prev = std::max (0, t - maxGap); prev <= t - minGap; ++prev)
        {
            const double d = std::log ((t - prev) / period);
            const double s = score[(size_t) prev] - dpTightness * d * d;
            if (s > best)
            {
                best = s;
                bestPrev = prev;
            }
        }
        score[(size_t) t] = o[(size_t) t] + best;
        back[(size_t) t] = bestPrev;
    }

    // end of the chain: best score within the last period
    int last = n - 1;
    for (int t = std::max (0, n - (int) std::ceil (period)); t < n; ++t)
        if (score[(size_t) t] > score[(size_t) last])
            last = t;

    std::vector<int> beats;
    for (int t = last; t >= 0; t = back[(size_t) t])
        beats.push_back (t);
    std::reverse (beats.begin(), beats.end());
    return beats;
}

//==============================================================================
/** 1 ms energy-rise envelope: rise[j] = max(0, E_j - E_{j-1}), E_j = energy of x[j*hop, j*hop+len), len = 20 ms.
    The onset of a sharp attack is at the trailing edge of the frame with the largest rise. */
struct AttackEnvelope
{
    int hop = 0, len = 0;
    std::vector<double> rise;

    double timeOf (int j, double sr) const { return (double) (j * hop + len) / sr; }
};

AttackEnvelope attackEnvelope (const float* x, int n, double sr)
{
    AttackEnvelope a;
    a.hop = std::max (1, (int) std::lround (sr / 1000.0));
    a.len = a.hop * 20; // 20 ms: longer than one cycle of a 50 Hz kick, so its tail does not "rise"
    const int frames = n > a.len ? (n - a.len) / a.hop + 1 : 0;
    a.rise.assign ((size_t) std::max (0, frames), 0.0);

    double prev = 0.0;
    for (int j = 0; j < frames; ++j)
    {
        double e = 0.0;
        for (int i = 0; i < a.len; ++i)
        {
            const double v = x[j * a.hop + i];
            e += v * v;
        }
        a.rise[(size_t) j] = j == 0 ? 0.0 : std::max (0.0, e - prev);
        prev = e;
    }
    return a;
}

/** Attack time near t (within the refine radius), with its rise strength. The attack starts at
    the earliest 1 ms step whose rise reaches half the local maximum: the maximum itself can sit
    anywhere in the first 20 ms of a low kick (sine peaks of similar energy). */
std::pair<double, double> refineToAttack (const AttackEnvelope& a, double t, double sr)
{
    const int centre = (int) std::lround ((t * sr - a.len) / a.hop);
    const int radius = (int) std::lround (refineRadiusSec * sr / a.hop);
    const int lo = std::max (1, centre - radius), hi = std::min ((int) a.rise.size() - 1, centre + radius);

    double maxRise = 0.0;
    for (int j = lo; j <= hi; ++j)
        maxRise = std::max (maxRise, a.rise[(size_t) j]);
    if (maxRise <= 0.0)
        return { t, 0.0 };

    for (int j = lo; j <= hi; ++j)
        if (a.rise[(size_t) j] >= 0.5 * maxRise)
            return { a.timeOf (j, sr), maxRise };

    return { t, 0.0 };
}

//==============================================================================
/** Linear low-band (< 150 Hz) energy rise per onset frame: kick / bass attacks. */
std::vector<double> lowBandRise (const float* x, int n, double sr, int hop)
{
    const auto spec = computeSpectrogram (x, n, sr, 2048, hop);
    const int lowBins = std::max (2, (int) (150.0 * spec.fftSize / sr));
    std::vector<double> rise ((size_t) spec.numFrames, 0.0);
    double prev = 0.0;
    for (int f = 0; f < spec.numFrames; ++f)
    {
        double e = 0.0;
        for (int b = 1; b <= lowBins; ++b)
            e += (double) spec.frame (f)[b] * spec.frame (f)[b];
        rise[(size_t) f] = f == 0 ? 0.0 : std::max (0.0, e - prev);
        prev = e;
    }
    return rise;
}

/** Sum of the strongest low-band rise within +-3 frames of each beat frame. */
double lowOnsetScore (const std::vector<double>& rise, const std::vector<double>& beatFrames)
{
    double s = 0.0;
    for (double bf : beatFrames)
    {
        const int c = (int) std::lround (bf);
        double best = 0.0;
        for (int f = std::max (0, c - 3); f <= std::min ((int) rise.size() - 1, c + 3); ++f)
            best = std::max (best, rise[(size_t) f]);
        s += best;
    }
    return s;
}

//==============================================================================
struct GridFit
{
    bool ok = false;
    double offset = 0.0, period = 0.0, rmsMs = 0.0, maxAbsMs = 0.0;
};

/** Least-squares t_i = offset + period * k_i with one outlier-rejection pass. */
GridFit fitGrid (const std::vector<double>& beats, double approxPeriod)
{
    GridFit fit;
    if (beats.size() < 8)
        return fit;

    std::vector<double> k (beats.size());
    for (size_t i = 0; i < beats.size(); ++i)
        k[i] = std::round ((beats[i] - beats[0]) / approxPeriod);

    std::vector<bool> use (beats.size(), true);
    for (int pass = 0; pass < 2; ++pass)
    {
        double sk = 0, st = 0, skk = 0, skt = 0, m = 0;
        for (size_t i = 0; i < beats.size(); ++i)
            if (use[i])
            {
                sk += k[i]; st += beats[i]; skk += k[i] * k[i]; skt += k[i] * beats[i]; m += 1;
            }
        const double denom = m * skk - sk * sk;
        if (m < 8 || denom <= 0.0)
            return fit;
        fit.period = (m * skt - sk * st) / denom;
        fit.offset = (st - fit.period * sk) / m;

        std::vector<double> residuals;
        for (size_t i = 0; i < beats.size(); ++i)
            residuals.push_back (std::abs (beats[i] - (fit.offset + fit.period * k[i])));

        auto sorted = residuals;
        std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
        const double med = sorted[sorted.size() / 2];

        double sq = 0.0, mx = 0.0, used = 0.0;
        for (size_t i = 0; i < beats.size(); ++i)
        {
            if (pass == 0)
                use[i] = residuals[i] <= std::max (0.003, 3.0 * med);
            if (use[i])
            {
                sq += residuals[i] * residuals[i];
                mx = std::max (mx, residuals[i]);
                used += 1;
            }
        }
        fit.rmsMs = 1000.0 * std::sqrt (sq / std::max (1.0, used));
        fit.maxAbsMs = 1000.0 * mx;
    }
    fit.ok = true;
    return fit;
}

//==============================================================================
std::vector<double> zScores (const std::vector<double>& v)
{
    if (v.empty())
        return v;
    const double mean = std::accumulate (v.begin(), v.end(), 0.0) / (double) v.size();
    double var = 0.0;
    for (double x : v)
        var += (x - mean) * (x - mean);
    const double sd = std::sqrt (var / (double) v.size());
    std::vector<double> z (v.size(), 0.0);
    if (sd > 1e-12)
        for (size_t i = 0; i < v.size(); ++i)
            z[i] = (v[i] - mean) / sd;
    return z;
}

double cosineDistance (const Chroma12& a, const Chroma12& b)
{
    double ab = 0, aa = 0, bb = 0;
    for (size_t i = 0; i < 12; ++i)
    {
        ab += (double) a[i] * b[i];
        aa += (double) a[i] * a[i];
        bb += (double) b[i] * b[i];
    }
    return aa > 0.0 && bb > 0.0 ? 1.0 - ab / std::sqrt (aa * bb) : 0.0;
}

double rmsAround (const float* x, int n, double sr, double t, double radius)
{
    const int a = std::max (0, (int) ((t - radius) * sr)), b = std::min (n, (int) ((t + radius) * sr));
    double s = 0.0;
    for (int i = a; i < b; ++i)
        s += (double) x[i] * x[i];
    return b > a ? std::sqrt (s / (b - a)) : 0.0;
}
} // namespace

//==============================================================================
RhythmResult analyzeRhythm (const float* x, int n, double sr)
{
    RhythmResult result;
    if (n < (int) (sr * 2.0))
        return result;

    double sq = 0.0;
    for (int i = 0; i < n; ++i)
        sq += (double) x[i] * x[i];
    const double trackRms = std::sqrt (sq / n);
    if (trackRms < 1.0e-5)
        return result;

    // --- tempo
    const auto env = onsetEnvelope (x, n, sr);
    const auto tempo = estimateTempo (env);
    if (tempo.bpm <= 0.0)
        return result;
    result.autocorrBpm = tempo.bpm;
    result.tempoConfidence = tempo.confidence;

    // --- beats: DP, then refine each beat to its attack
    const auto attack = attackEnvelope (x, n, sr);
    std::vector<double> beats, strength;
    std::vector<double> beatFrames;
    for (int f : trackBeats (env, tempo.bpm))
        beatFrames.push_back (f);

    // On/off-beat check: flux + DP cannot tell beats from 8th-note offbeats when offbeat hats
    // dominate (house). Beats are where the low end (kick / bass attacks) hits, so keep the
    // half-beat shift only if it lands clearly more low-band energy.
    {
        const auto rise = lowBandRise (x, n, sr, (int) std::lround (sr / env.fps));
        const double halfPeriod = 30.0 * env.fps / tempo.bpm;
        auto shifted = beatFrames;
        for (auto& f : shifted)
            f += halfPeriod;
        while (! shifted.empty() && shifted.back() >= (double) env.values.size())
            shifted.pop_back();
        if (lowOnsetScore (rise, shifted) > 1.25 * lowOnsetScore (rise, beatFrames))
            beatFrames = std::move (shifted);
    }

    for (double f : beatFrames)
    {
        const auto [t, rise] = refineToAttack (attack, f / env.fps, sr);
        beats.push_back (t);
        strength.push_back (rise);
    }

    // drop leading/trailing beats in silence or without an attack (e.g. a release tail)
    const double floorRms = trackRms * std::pow (10.0, silenceDb / 20.0);
    const double beatSec = 60.0 / tempo.bpm;
    double minAttack = 0.0;
    if (! strength.empty())
    {
        auto sorted = strength;
        std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
        minAttack = 0.25 * sorted[sorted.size() / 2];
    }
    auto keep = [&] (size_t i) { return strength[i] > 0.0 && strength[i] >= minAttack && rmsAround (x, n, sr, beats[i], 0.1) >= floorRms; };
    size_t first = 0, last = beats.size();
    while (first < last && ! keep (first)) ++first;
    while (last > first && ! keep (last - 1)) --last;
    beats = { beats.begin() + (long) first, beats.begin() + (long) last };
    strength = { strength.begin() + (long) first, strength.begin() + (long) last };
    if (beats.size() < 2)
        return result;

    // --- constant-tempo grid
    const auto fit = fitGrid (beats, beatSec);
    result.fixedTempo = fit.ok && fit.rmsMs < 10.0 && fit.maxAbsMs < 30.0;
    result.gridResidualMs = fit.rmsMs;
    result.bpm = result.fixedTempo ? 60.0 / fit.period : tempo.bpm;

    if (result.fixedTempo)
    {
        for (size_t i = 0; i < beats.size(); ++i)
            beats[i] = fit.offset + fit.period * std::round ((beats[i] - fit.offset) / fit.period);

        // The onset envelope cannot see an attack at t = 0 (the centred first frame already
        // contains it), so a grid that starts right at the top of the file loses beat 1.
        // Extend the fitted grid at both ends where there is an actual attack on the grid.
        const double durationSec = n / sr;

        for (double t = beats.front() - fit.period; t > -0.002; t -= fit.period)
        {
            const double a = refineToAttack (attack, std::max (0.0, t), sr).second;
            if (a < minAttack || a <= 0.0)
                break;
            beats.insert (beats.begin(), std::max (0.0, t));
            strength.insert (strength.begin(), a);
        }
        for (double t = beats.back() + fit.period; t < durationSec; t += fit.period)
        {
            const double a = refineToAttack (attack, t, sr).second;
            if (a < minAttack || a <= 0.0)
                break;
            beats.push_back (t);
            strength.push_back (a);
        }
    }

    for (double alias : { result.bpm / 2.0, result.bpm * 2.0 })
        if (alias >= minBpm && alias <= maxBpm)
            result.tempoAliases.push_back (alias);

    // --- downbeat phase (4/4): harmonic change + attack accents + low-frequency accents
    result.chroma = computeChroma (x, n, sr);
    const auto& chroma = result.chroma;
    result.tuningCents = chroma.tuningCents;

    const size_t nb = beats.size();
    std::vector<double> harmonicChange (nb, 0.0), lowAccent (nb, 0.0);
    {
        const auto lowSpec = computeSpectrogram (x, n, sr, 2048, (int) std::lround (sr / 100.0));
        const int lowBins = std::max (2, (int) (200.0 * lowSpec.fftSize / sr));
        for (size_t i = 0; i < nb; ++i)
        {
            const double before = i >= 2 ? beats[i - 2] : beats[i] - 2.0 * beatSec;
            const double after = i + 2 < nb ? beats[i + 2] : beats[i] + 2.0 * beatSec;
            harmonicChange[i] = cosineDistance (sumChroma (chroma, before, beats[i]), sumChroma (chroma, beats[i], after));

            // low-band energy just after vs. just before the beat
            auto lowEnergy = [&] (double t)
            {
                const int f = std::clamp ((int) std::lround (t * sr / lowSpec.hop), 0, lowSpec.numFrames - 1);
                double e = 0.0;
                for (int b = 1; b < lowBins; ++b)
                    e += (double) lowSpec.frame (f)[b] * lowSpec.frame (f)[b];
                return e;
            };
            lowAccent[i] = std::max (0.0, lowEnergy (beats[i] + 0.03) - lowEnergy (beats[i] - 0.05));
        }
    }

    const auto zh = zScores (harmonicChange), za = zScores (strength), zl = zScores (lowAccent);
    double bestScore = -1.0e300;
    for (int phase = 0; phase < result.beatsPerBar; ++phase)
    {
        double s = 0.0;
        int count = 0;
        for (size_t i = (size_t) phase; i < nb; i += (size_t) result.beatsPerBar)
        {
            s += zh[i] + 0.5 * za[i] + 0.5 * zl[i];
            ++count;
        }
        if (count > 0 && s / count > bestScore)
        {
            bestScore = s / count;
            result.downbeatPhase = phase;
        }
    }

    for (size_t i = (size_t) result.downbeatPhase; i < nb; i += (size_t) result.beatsPerBar)
        result.downbeats.push_back (beats[i]);

    // --- bars + per-bar chroma
    const double durationSec = n / sr;
    for (size_t d = 0; d < result.downbeats.size(); ++d)
    {
        const double start = result.downbeats[d];
        const double end = d + 1 < result.downbeats.size() ? result.downbeats[d + 1]
                                                             : start + result.beatsPerBar * 60.0 / result.bpm;
        if (end > durationSec + 0.05)
            break;
        result.bars.push_back ({ start, end, normalisedMax (sumChroma (chroma, start, end)) });
    }

    result.beats = std::move (beats);
    return result;
}

} // namespace jaudios::analysis
