#include "CQT.h"

#include "engine/analysis/Resampler.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace jaudios::dsp
{

namespace
{
constexpr double pi = 3.14159265358979323846;

int numTwoFactors (int x)
{
    int n = 0;
    while (x > 0 && x % 2 == 0)
    {
        ++n;
        x /= 2;
    }
    return n;
}

/** scipy.signal.get_window('hann', n, fftbins=True): periodic Hann. */
std::vector<double> periodicHann (int n)
{
    std::vector<double> w ((size_t) n);
    for (int i = 0; i < n; ++i)
        w[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * pi * i / n);
    return w;
}

/** In-place complex FFT of size n (power of two) via juce::dsp::FFT. */
void complexFft (std::vector<std::complex<double>>& x)
{
    const int n = (int) x.size();
    juce::dsp::FFT fft ((int) std::lround (std::log2 (n)));
    std::vector<juce::dsp::Complex<float>> in ((size_t) n), out ((size_t) n);
    // Complex basis in double; JUCE FFT is float. Split into two float passes (hi + lo) to keep
    // ~1e-10 relative precision.
    std::vector<juce::dsp::Complex<float>> inLo ((size_t) n), outLo ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const auto hi = juce::dsp::Complex<float> ((float) x[(size_t) i].real(), (float) x[(size_t) i].imag());
        in[(size_t) i] = hi;
        inLo[(size_t) i] = { (float) (x[(size_t) i].real() - hi.real()), (float) (x[(size_t) i].imag() - hi.imag()) };
    }
    fft.perform (in.data(), out.data(), false);
    fft.perform (inLo.data(), outLo.data(), false);
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = { (double) out[(size_t) i].real() + outLo[(size_t) i].real(),
                          (double) out[(size_t) i].imag() + outLo[(size_t) i].imag() };
}

std::vector<float> halve (const std::vector<float>& y)
{
    // librosa: resample(y, orig_sr=2, target_sr=1, res_type='soxr_hq', scale=True)
    //   -> length ceil(n / 2), then y /= sqrt(1 / 2)
    juce::AudioBuffer<float> in (1, (int) y.size());
    in.copyFrom (0, 0, y.data(), (int) y.size());
    const auto out = analysis::resample (in, 2.0, 1.0);
    const int n = ((int) y.size() + 1) / 2;
    std::vector<float> r ((size_t) n, 0.0f);
    const float scale = (float) std::sqrt (2.0);
    for (int i = 0; i < std::min (n, out.getNumSamples()); ++i)
        r[(size_t) i] = out.getSample (0, i) * scale;
    return r;
}
} // namespace

ConstantQ::ConstantQ (Config c) : cfg (c)
{
    const int n = cfg.numBins;
    freqs.resize ((size_t) n);
    for (int k = 0; k < n; ++k)
        freqs[(size_t) k] = cfg.fmin * std::pow (2.0, (double) k / cfg.binsPerOctave); // tuning = 0

    // filters._relative_bandwidth
    alphas.resize ((size_t) n);
    std::vector<double> bpo ((size_t) n);
    bpo[0] = 1.0 / (std::log2 (freqs[1]) - std::log2 (freqs[0]));
    bpo[(size_t) n - 1] = 1.0 / (std::log2 (freqs[(size_t) n - 1]) - std::log2 (freqs[(size_t) n - 2]));
    for (int k = 1; k < n - 1; ++k)
        bpo[(size_t) k] = 2.0 / (std::log2 (freqs[(size_t) k + 1]) - std::log2 (freqs[(size_t) k - 1]));
    for (int k = 0; k < n; ++k)
    {
        const double p = std::pow (2.0, 2.0 / bpo[(size_t) k]);
        alphas[(size_t) k] = (p - 1.0) / (p + 1.0);
    }

    // filters.wavelet_lengths (gamma = 0): Q * sr / f, Q = filter_scale / alpha
    lengths.resize ((size_t) n);
    for (int k = 0; k < n; ++k)
        lengths[(size_t) k] = cfg.filterScale / alphas[(size_t) k] * cfg.sampleRate / freqs[(size_t) k];

    // Early downsampling (vqt.__early_downsample_count). Hann window bandwidth 1.50018310546875.
    const int numOctaves = (int) std::ceil ((double) n / cfg.binsPerOctave);
    double cutoff = 0.0;
    for (int k = 0; k < n; ++k)
        cutoff = std::max (cutoff, freqs[(size_t) k] * (1.0 + 0.5 * 1.50018310546875 * alphas[(size_t) k] / cfg.filterScale));
    const int count1 = std::max (0, (int) std::ceil (std::log2 (cfg.sampleRate / 2.0 / cutoff)) - 1 - 1);
    const int count2 = std::max (0, numTwoFactors (cfg.hopLength) - numOctaves + 1);
    jassert (std::min (count1, count2) == 0); // true for the OpenKeyScan config; not implemented otherwise
    juce::ignoreUnused (count1, count2);

    double mySr = cfg.sampleRate;
    int myHop = cfg.hopLength, decimations = 0;
    const int nFilters = std::min (cfg.binsPerOctave, n);

    for (int i = 0; i < numOctaves; ++i)
    {
        Octave oct;
        const int end = n - nFilters * i;
        oct.firstBin = std::max (0, end - nFilters);
        oct.numBins = end - oct.firstBin;
        oct.sampleRate = mySr;
        oct.hop = myHop;
        oct.decimationsBefore = decimations;

        // filters.wavelet at the octave's rate (alpha from the full frequency set)
        std::vector<double> octLengths;
        for (int b = oct.firstBin; b < end; ++b)
            octLengths.push_back (cfg.filterScale / alphas[(size_t) b] * mySr / freqs[(size_t) b]);
        const double maxLen = *std::max_element (octLengths.begin(), octLengths.end());
        oct.nfft = (int) std::pow (2.0, std::ceil (std::log2 (maxLen)));

        for (int j = 0; j < oct.numBins; ++j)
        {
            const int b = oct.firstBin + j;
            const double len = octLengths[(size_t) j];
            const auto start = (long) std::floor (-len / 2.0), stop = (long) std::floor (len / 2.0);
            const int count = (int) (stop - start);
            const auto window = periodicHann (count);
            const double l1 = std::accumulate (window.begin(), window.end(), 0.0); // |phasor| = 1

            std::vector<std::complex<double>> basis ((size_t) oct.nfft, { 0.0, 0.0 });
            const int lpad = (oct.nfft - count) / 2; // util.pad_center
            for (int m = 0; m < count; ++m)
            {
                const double phase = (double) (start + m) * 2.0 * pi * freqs[(size_t) b] / mySr;
                basis[(size_t) (lpad + m)] = std::polar (window[(size_t) m] / l1, phase) * (len / oct.nfft);
            }

            complexFft (basis);
            const int half = oct.nfft / 2 + 1;

            // util.sparsify_rows(quantile)
            std::vector<double> mags ((size_t) half);
            for (int k = 0; k < half; ++k)
                mags[(size_t) k] = std::abs (basis[(size_t) k]);
            auto sorted = mags;
            std::sort (sorted.begin(), sorted.end());
            const double norm = std::accumulate (sorted.begin(), sorted.end(), 0.0);
            double cumulative = 0.0;
            size_t thresholdIdx = 0;
            for (; thresholdIdx < sorted.size(); ++thresholdIdx)
            {
                cumulative += sorted[thresholdIdx] / norm;
                if (! (cumulative < cfg.sparsity))
                    break;
            }
            const double threshold = sorted[std::min (thresholdIdx, sorted.size() - 1)];

            SparseRow row;
            const double rateScale = std::sqrt (cfg.sampleRate / mySr);
            for (int k = 0; k < half; ++k)
                if (mags[(size_t) k] >= threshold)
                {
                    row.bins.push_back (k);
                    row.weights.push_back (basis[(size_t) k] * rateScale);
                }
            oct.rows.push_back (std::move (row));
        }

        octaves.push_back (std::move (oct));

        if (myHop % 2 == 0)
        {
            myHop /= 2;
            mySr /= 2.0;
            ++decimations;
        }
    }
}

CqtMatrix ConstantQ::magnitude (const float* mono, int numSamples) const
{
    std::vector<std::vector<std::complex<double>>> responses (octaves.size());
    std::vector<int> frameCounts (octaves.size());

    std::vector<float> signal (mono, mono + numSamples);
    int decimationsDone = 0;

    for (size_t o = 0; o < octaves.size(); ++o)
    {
        const auto& oct = octaves[o];
        while (decimationsDone < oct.decimationsBefore)
        {
            signal = halve (signal);
            ++decimationsDone;
        }

        const int len = (int) signal.size();
        const int frames = 1 + len / oct.hop;
        frameCounts[o] = frames;
        auto& resp = responses[o];
        resp.assign ((size_t) oct.numBins * (size_t) frames, { 0.0, 0.0 });

        juce::dsp::FFT fft ((int) std::lround (std::log2 (oct.nfft)));
        std::vector<float> buf ((size_t) oct.nfft * 2);

        for (int t = 0; t < frames; ++t)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            const int start = t * oct.hop - oct.nfft / 2; // centred, zero padded, rectangular window
            for (int m = 0; m < oct.nfft; ++m)
            {
                const int idx = start + m;
                if (idx >= 0 && idx < len)
                    buf[(size_t) m] = signal[(size_t) idx];
            }
            fft.performRealOnlyForwardTransform (buf.data(), true); // interleaved re/im, bins 0..n/2

            for (int j = 0; j < oct.numBins; ++j)
            {
                std::complex<double> acc { 0.0, 0.0 };
                const auto& row = oct.rows[(size_t) j];
                for (size_t q = 0; q < row.bins.size(); ++q)
                {
                    const int k = row.bins[q];
                    acc += row.weights[q] * std::complex<double> (buf[(size_t) (2 * k)], buf[(size_t) (2 * k + 1)]);
                }
                resp[(size_t) j * (size_t) frames + (size_t) t] = acc;
            }
        }
    }

    CqtMatrix out;
    out.numBins = cfg.numBins;
    out.numFrames = *std::min_element (frameCounts.begin(), frameCounts.end());
    out.values.assign ((size_t) out.numBins * (size_t) out.numFrames, 0.0f);

    for (size_t o = 0; o < octaves.size(); ++o)
    {
        const auto& oct = octaves[o];
        for (int j = 0; j < oct.numBins; ++j)
        {
            const int b = oct.firstBin + j;
            const double scale = 1.0 / std::sqrt (lengths[(size_t) b]);
            for (int t = 0; t < out.numFrames; ++t)
                out.at (b, t) = (float) (std::abs (responses[o][(size_t) j * (size_t) frameCounts[o] + (size_t) t]) * scale);
        }
    }
    return out;
}

CqtMatrix openKeyScanModelInput (const ConstantQ& cqt, const float* mono44k, int numSamples)
{
    const auto mag = cqt.magnitude (mono44k, numSamples);
    CqtMatrix in;
    in.numBins = mag.numBins;
    in.numFrames = std::max (0, mag.numFrames - 2); // spec[:, 0:-2]
    in.values.resize ((size_t) in.numBins * (size_t) in.numFrames);
    for (int b = 0; b < in.numBins; ++b)
        for (int t = 0; t < in.numFrames; ++t)
            in.at (b, t) = std::log1p (mag.at (b, t));
    return in;
}

} // namespace jaudios::dsp
