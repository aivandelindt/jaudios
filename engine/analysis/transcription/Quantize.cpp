#include "Quantize.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace jaudios::analysis::transcription
{

namespace
{
/** Grid over `beats`: position (in steps, fractional) of time t, and time of a step index. */
struct Grid
{
    const std::vector<double>& beats;
    int stepsPerBeat;

    double beatLength (size_t i) const noexcept { return beats[i + 1] - beats[i]; }

    double stepsAt (double t) const noexcept
    {
        const size_t n = beats.size();
        if (t <= beats.front())
            return (t - beats.front()) / beatLength (0) * stepsPerBeat;
        if (t >= beats.back())
            return ((double) (n - 1) + (t - beats.back()) / beatLength (n - 2)) * stepsPerBeat;
        const auto it = std::upper_bound (beats.begin(), beats.end(), t);
        const auto i = (size_t) (it - beats.begin()) - 1;
        return ((double) i + (t - beats[i]) / beatLength (i)) * stepsPerBeat;
    }

    double timeAt (double steps) const noexcept
    {
        const size_t n = beats.size();
        const double beat = steps / stepsPerBeat;
        if (beat <= 0.0)
            return beats.front() + beat * beatLength (0);
        if (beat >= (double) (n - 1))
            return beats.back() + (beat - (double) (n - 1)) * beatLength (n - 2);
        const auto i = (size_t) std::floor (beat);
        return beats[i] + (beat - (double) i) * beatLength (i);
    }
};

double median (std::vector<double> v)
{
    std::sort (v.begin(), v.end());
    const size_t m = v.size() / 2;
    return v.size() % 2 ? v[m] : 0.5 * (v[m - 1] + v[m]);
}

/** Nearest grid position (in steps) when odd steps are delayed by `swing` steps. */
double snap (double steps, double swing) noexcept
{
    const double pair = std::floor (steps / 2.0) * 2.0;
    double best = pair, bestDist = 1e9;
    for (const double c : { pair, pair + 1.0 + swing, pair + 2.0, pair - 1.0 + swing })
        if (const double d = std::abs (steps - c); d < bestDist)
        {
            bestDist = d;
            best = c;
        }
    return best;
}
} // namespace

QuantizeResult quantize (std::vector<Note> notes, const std::vector<double>& beats, const QuantizeSettings& s)
{
    QuantizeResult r;
    if (beats.size() < 2 || s.division < 4 || notes.empty())
    {
        r.notes = std::move (notes);
        return r;
    }
    const Grid grid { beats, std::max (1, s.division / 4) };

    if (s.preserveSwing)
    {
        // relative to the on-step notes, so a constant latency (all notes late) is not swing
        std::vector<double> odd, even;
        for (const auto& n : notes)
        {
            const double x = grid.stepsAt (n.startSec);
            const double nearest = std::round (x);
            (std::fmod (std::abs (nearest), 2.0) == 1.0 ? odd : even).push_back (x - nearest);
        }
        if (odd.size() >= 4)
            if (const double m = median (odd) - (even.size() >= 4 ? median (even) : 0.0); m > 0.05)
                r.swing = std::min (m, 0.5);
    }

    const double strength = std::clamp (s.strength, 0.0, 1.0);
    for (auto& n : notes)
    {
        const double start = grid.timeAt (snap (grid.stepsAt (n.startSec), r.swing));
        double end = n.endSec;
        if (s.quantizeEnds)
        {
            const double startSteps = snap (grid.stepsAt (n.startSec), r.swing);
            const double endSteps = std::max (snap (grid.stepsAt (n.endSec), r.swing), startSteps + 1.0);
            end = grid.timeAt (endSteps);
        }
        const double newStart = n.startSec + strength * (start - n.startSec);
        const double newEnd = n.endSec + strength * (end - n.endSec);
        n.startSec = newStart;
        n.endSec = std::max (newEnd, newStart + 1e-3);
    }
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return std::tie (a.startSec, a.pitch) < std::tie (b.startSec, b.pitch); });
    r.notes = std::move (notes);
    return r;
}

} // namespace jaudios::analysis::transcription
