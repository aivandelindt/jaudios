#include "KickDetector.h"

#include <algorithm>
#include <cmath>

namespace jaudios::analysis
{

std::vector<double> detectKicks (const float* x, int n, double sr, const KickSettings& s)
{
    std::vector<double> kicks;
    if (n <= 0 || sr <= 0.0)
        return kicks;

    // 2nd-order Butterworth low-pass (RBJ cookbook), run twice: 4th order, steep enough to keep hats out
    const double w0 = 2.0 * 3.14159265358979323846 * s.lowpassHz / sr;
    const double alpha = std::sin (w0) / (2.0 * std::sqrt (0.5));
    const double cw = std::cos (w0), a0 = 1.0 + alpha;
    const double b0 = (1.0 - cw) / 2.0 / a0, b1 = (1.0 - cw) / a0, b2 = b0, a1 = -2.0 * cw / a0, a2 = (1.0 - alpha) / a0;
    std::vector<double> y (x, x + n);
    for (int pass = 0; pass < 2; ++pass)
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (int i = 0; i < n; ++i)
        {
            const double in = y[(size_t) i];
            const double out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = in; y2 = y1; y1 = out;
            y[(size_t) i] = out;
        }
    }

    // frame energy (dB) at 256-sample hops (5.8 ms at 44.1 kHz)
    const int hop = std::max (1, (int) std::lround (256.0 * sr / 44100.0));
    const int frames = n / hop;
    if (frames < 3)
        return kicks;
    std::vector<double> db ((size_t) frames);
    double loudest = -200.0;
    for (int f = 0; f < frames; ++f)
    {
        double e = 0.0;
        for (int i = f * hop; i < (f + 1) * hop; ++i)
            e += y[(size_t) i] * y[(size_t) i];
        db[(size_t) f] = 10.0 * std::log10 (e / hop + 1e-12);
        loudest = std::max (loudest, db[(size_t) f]);
    }

    // onset strength: energy rise over two frames (~12 ms: the kick's attack), half-wave rectified
    std::vector<double> rise ((size_t) frames, 0.0);
    for (int f = 2; f < frames; ++f)
        rise[(size_t) f] = std::max (0.0, db[(size_t) f] - std::min (db[(size_t) f - 1], db[(size_t) f - 2]));
    std::vector<double> sorted (rise);
    std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
    const double threshold = sorted[sorted.size() / 2] + s.thresholdDb;

    const int minGap = std::max (1, (int) std::lround (s.minIntervalSec * sr / hop));
    int last = -minGap;
    for (int f = 2; f + 1 < frames; ++f)
    {
        const double r = rise[(size_t) f];
        if (r < threshold || r < rise[(size_t) f - 1] || r <= rise[(size_t) f + 1])
            continue;
        // the energy the onset reaches (a few frames on) must be loud enough
        double peak = db[(size_t) f];
        for (int k = f; k < std::min (frames, f + 4); ++k)
            peak = std::max (peak, db[(size_t) k]);
        if (peak < loudest + s.floorDb || peak < s.absoluteFloorDb || f - last < minGap)
            continue;
        kicks.push_back ((double) (f - 1) * hop / sr); // the rise starts in the frame before
        last = f;
    }
    return kicks;
}

} // namespace jaudios::analysis
