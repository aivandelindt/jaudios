#include "NoteCreation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace jaudios::analysis::transcription
{

using G = BasicPitchGeometry;

namespace
{
constexpr int maxFreqIdx = G::noteBins - 1; // MAX_FREQ_IDX = 87

/** np.round: half to even. */
inline double roundHalfEven (double x) noexcept { return std::nearbyint (x); }

/** librosa.hz_to_midi */
inline double hzToMidi (double hz) noexcept { return 12.0 * (std::log2 (hz) - std::log2 (440.0)) + 69.0; }

/** Frame-major matrix of doubles: upstream promotes everything to float64 (np.zeros / concatenate). */
struct Matrix
{
    int rows = 0, cols = 0;
    std::vector<double> v;
    Matrix (int r, int c) : rows (r), cols (c), v ((size_t) r * (size_t) c, 0.0) {}
    double& at (int t, int f) noexcept { return v[(size_t) t * (size_t) cols + (size_t) f]; }
    double at (int t, int f) const noexcept { return v[(size_t) t * (size_t) cols + (size_t) f]; }
};

/** np.mean(frames[a:b, f]) */
double meanColumn (const Posteriorgram& pg, int a, int b, int f) noexcept
{
    double sum = 0.0;
    for (int t = a; t < b; ++t)
        sum += pg.noteAt (t, f);
    return b > a ? sum / (b - a) : std::numeric_limits<double>::quiet_NaN();
}

void zeroNeighbourhood (Matrix& e, int t, int f) noexcept
{
    e.at (t, f) = 0.0;
    if (f < maxFreqIdx)
        e.at (t, f + 1) = 0.0;
    if (f > 0)
        e.at (t, f - 1) = 0.0;
}
} // namespace

int NoteParams::minNoteFrames() const noexcept
{
    return (int) roundHalfEven (minNoteMs / 1000.0 * ((double) G::sampleRate / G::fftHop));
}

double frameToSeconds (int frame) noexcept
{
    // librosa.frames_to_time(i, sr, hop) - window_offset * floor(i / ANNOT_N_FRAMES); upstream's
    // "magic number" 0.0018 included.
    const double windowOffset = ((double) G::fftHop / G::sampleRate) * (G::windowFrames - (double) G::windowSamples / G::fftHop) + 0.0018;
    return (double) frame * G::fftHop / G::sampleRate - windowOffset * std::floor ((double) frame / G::windowFrames);
}

std::vector<FrameNote> extractNotes (const Posteriorgram& pg, const NoteParams& p)
{
    const int n = pg.frames;
    const int bins = G::noteBins;
    std::vector<FrameNote> notes;
    if (n <= 0)
        return notes;

    Matrix onsets (n, bins), frames (n, bins);
    for (int t = 0; t < n; ++t)
        for (int f = 0; f < bins; ++f)
        {
            onsets.at (t, f) = pg.onsetAt (t, f);
            frames.at (t, f) = pg.noteAt (t, f);
        }

    // constrain_frequency (upstream zeroes these bins in place, so `frames` below is constrained too)
    int lo = 0, hi = bins;
    if (p.maxFrequencyHz)
        hi = std::clamp ((int) roundHalfEven (hzToMidi (*p.maxFrequencyHz) - G::midiOffset), 0, bins);
    if (p.minFrequencyHz)
        lo = std::clamp ((int) roundHalfEven (hzToMidi (*p.minFrequencyHz) - G::midiOffset), 0, bins);
    for (int t = 0; t < n; ++t)
        for (int f = 0; f < bins; ++f)
            if (f >= hi || f < lo)
                onsets.at (t, f) = frames.at (t, f) = 0.0;

    // get_infered_onsets(n_diff=2)
    if (p.inferOnsets)
    {
        Matrix diff (n, bins);
        double maxDiff = 0.0, maxOnset = -std::numeric_limits<double>::infinity();
        for (int t = 0; t < n; ++t)
            for (int f = 0; f < bins; ++f)
            {
                maxOnset = std::max (maxOnset, (double) (float) onsets.at (t, f)); // np.max of the float32 array
                double d = std::numeric_limits<double>::infinity();
                for (int k = 1; k <= 2; ++k)
                    d = std::min (d, frames.at (t, f) - (t - k >= 0 ? frames.at (t - k, f) : 0.0));
                d = t < 2 ? 0.0 : std::max (0.0, d);
                diff.at (t, f) = d;
                maxDiff = std::max (maxDiff, d);
            }
        if (maxDiff == 0.0)
            return notes; // upstream divides by zero: every onset becomes NaN, so no note is found
        for (int t = 0; t < n; ++t)
            for (int f = 0; f < bins; ++f)
                onsets.at (t, f) = std::max (onsets.at (t, f), maxOnset * diff.at (t, f) / maxDiff);
    }

    // scipy.signal.argrelmax(axis=0, mode='clip') >= onset_thresh; upstream walks them backwards in time
    std::vector<std::pair<int, int>> onsetIdx;
    for (int t = 1; t + 1 < n; ++t)
        for (int f = 0; f < bins; ++f)
        {
            const double x = onsets.at (t, f);
            if (x > onsets.at (t - 1, f) && x > onsets.at (t + 1, f) && x >= p.onsetThreshold)
                onsetIdx.emplace_back (t, f);
        }
    std::reverse (onsetIdx.begin(), onsetIdx.end());

    Matrix energy = frames; // remaining_energy
    const int minLen = p.minNoteFrames();
    const double thr = p.frameThreshold;
    const int tol = p.energyTolerance;

    for (const auto& [start, f] : onsetIdx)
    {
        if (start >= n - 1)
            continue;
        int i = start + 1, k = 0;
        while (i < n - 1 && k < tol)
        {
            k = energy.at (i, f) < thr ? k + 1 : 0;
            ++i;
        }
        i -= k;
        if (i - start <= minLen)
            continue;
        for (int t = start; t < i; ++t)
            zeroNeighbourhood (energy, t, f);
        notes.push_back ({ start, i, f + G::midiOffset, meanColumn (pg, start, i, f), {} });
    }

    if (p.melodiaTrick)
    {
        for (;;)
        {
            // np.argmax: first maximum in C order
            int tm = 0, fm = 0;
            double best = -std::numeric_limits<double>::infinity();
            for (int t = 0; t < n; ++t)
                for (int f = 0; f < bins; ++f)
                    if (energy.at (t, f) > best)
                    {
                        best = energy.at (t, f);
                        tm = t;
                        fm = f;
                    }
            if (! (best > thr))
                break;
            energy.at (tm, fm) = 0.0;

            int i = tm + 1, k = 0;
            while (i < n - 1 && k < tol)
            {
                k = energy.at (i, fm) < thr ? k + 1 : 0;
                zeroNeighbourhood (energy, i, fm);
                ++i;
            }
            const int end = i - 1 - k;

            i = tm - 1;
            k = 0;
            while (i > 0 && k < tol)
            {
                k = energy.at (i, fm) < thr ? k + 1 : 0;
                zeroNeighbourhood (energy, i, fm);
                --i;
            }
            const int start = i + 1 + k;

            if (end - start <= minLen)
                continue;
            notes.push_back ({ start, end, fm + G::midiOffset, meanColumn (pg, start, end, fm), {} });
        }
    }

    // get_pitch_bends(n_bins_tolerance=25): argmax of contour x Gaussian(51, std 5) around the note
    constexpr int tolBins = 25, window = 2 * tolBins + 1;
    double gaussian[window];
    for (int j = 0; j < window; ++j)
    {
        const double x = j - (window - 1) / 2.0;
        gaussian[j] = std::exp (-0.5 * x * x / 25.0);
    }
    for (auto& note : notes)
    {
        const double hz = 440.0 * std::pow (2.0, (note.pitch - 69.0) / 12.0);
        const int freqIdx = (int) roundHalfEven (12.0 * 3 * std::log2 (hz / 27.5));
        const int fs = std::max (freqIdx - tolBins, 0);
        const int fe = std::min (G::contourBins, freqIdx + tolBins + 1);
        const int gs = std::max (0, tolBins - freqIdx);
        const int shift = tolBins - std::max (0, tolBins - freqIdx);
        note.bends.reserve ((size_t) (note.end - note.start));
        for (int t = note.start; t < note.end; ++t)
        {
            int arg = 0;
            double best = -std::numeric_limits<double>::infinity();
            for (int c = fs; c < fe; ++c)
            {
                const double v = (double) pg.contour[(size_t) t * G::contourBins + (size_t) c] * gaussian[gs + (c - fs)];
                if (v > best)
                {
                    best = v;
                    arg = c - fs;
                }
            }
            note.bends.push_back (arg - shift);
        }
    }
    return notes;
}

} // namespace jaudios::analysis::transcription
