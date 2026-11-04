#include "Resampler.h"

#include <cmath>
#include <vector>

namespace jaudios::analysis
{

namespace
{
constexpr int zeroCrossings = 32;   // half-width of the kernel in (cutoff-scaled) samples
constexpr int tableResolution = 512; // table entries per zero crossing
constexpr double kaiserBeta = 9.0;   // ~90 dB stopband
constexpr double rolloff = 0.95;     // cutoff as a fraction of the lower Nyquist

double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < sum * 1.0e-17)
            break;
    }
    return sum;
}

/** h(x) = sinc(x) * kaiser(x / zeroCrossings), sampled at x = i / tableResolution. */
const std::vector<float>& kernelTable()
{
    static const std::vector<float> table = []
    {
        std::vector<float> t ((size_t) (zeroCrossings * tableResolution + 2), 0.0f);
        const double i0Beta = besselI0 (kaiserBeta);
        for (size_t i = 0; i < t.size(); ++i)
        {
            const double x = (double) i / tableResolution;
            if (x >= zeroCrossings)
                break;
            const double sinc = x == 0.0 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
            const double r = x / zeroCrossings;
            const double window = besselI0 (kaiserBeta * std::sqrt (1.0 - r * r)) / i0Beta;
            t[i] = (float) (sinc * window);
        }
        return t;
    }();
    return table;
}

inline double kernel (const std::vector<float>& table, double x)
{
    x = std::abs (x) * tableResolution;
    const auto i = (size_t) x;
    if (i + 1 >= table.size())
        return 0.0;
    const double frac = x - (double) i;
    return table[i] + frac * (table[i + 1] - table[i]);
}
} // namespace

juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& input, double sourceRate, double targetRate)
{
    jassert (sourceRate > 0.0 && targetRate > 0.0);

    const int numChannels = input.getNumChannels();
    const int numIn = input.getNumSamples();

    if (sourceRate == targetRate)
        return juce::AudioBuffer<float> (input);

    const int numOut = (int) std::llround ((double) numIn * targetRate / sourceRate);
    juce::AudioBuffer<float> output (numChannels, numOut);

    const auto& table = kernelTable();
    const double step = sourceRate / targetRate;                    // input samples per output sample
    const double cutoff = rolloff * std::min (1.0, targetRate / sourceRate); // in cycles per input sample * 2
    const double halfWidth = zeroCrossings / cutoff;                // kernel half-width in input samples

    std::vector<double> weights;
    weights.reserve ((size_t) (2.0 * halfWidth) + 4);

    for (int k = 0; k < numOut; ++k)
    {
        const double t = k * step;
        const int first = std::max (0, (int) std::ceil (t - halfWidth));
        const int last = std::min (numIn - 1, (int) std::floor (t + halfWidth));

        weights.clear();
        for (int n = first; n <= last; ++n)
            weights.push_back (cutoff * kernel (table, (t - n) * cutoff));

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* x = input.getReadPointer (ch);
            double acc = 0.0;
            for (int n = first, w = 0; n <= last; ++n, ++w)
                acc += weights[(size_t) w] * x[n];
            output.setSample (ch, k, (float) acc);
        }
    }

    return output;
}

} // namespace jaudios::analysis
