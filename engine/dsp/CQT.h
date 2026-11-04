#pragma once

#include <complex>
#include <vector>

namespace jaudios::dsp
{

/** Bin-major magnitude matrix: values[bin * numFrames + frame]. */
struct CqtMatrix
{
    int numBins = 0, numFrames = 0;
    std::vector<float> values;

    float at (int bin, int frame) const noexcept { return values[(size_t) bin * (size_t) numFrames + (size_t) frame]; }
    float& at (int bin, int frame) noexcept { return values[(size_t) bin * (size_t) numFrames + (size_t) frame]; }
};

/** Port of librosa 0.11.0 `cqt` (= `vqt` with gamma = 0), ISC licensed, for the configuration the
    OpenKeyScan model was trained on. Reproduces, step by step:
      - frequencies fmin * 2^(k / bpo), relative bandwidth alpha (filters._relative_bandwidth),
        wavelet lengths Q * sr / f (filters.wavelet_lengths)
      - per-octave processing from the top octave down: Hann-windowed complex wavelets (periodic
        window, L1-normalised, centred in a power-of-two n_fft), scaled by length / n_fft, FFT,
        row-sparsified at quantile 0.01 (util.sparsify_rows), times sqrt(sr / octave_sr)
      - rectangular-window centred STFT (zero padding) per octave, basis . STFT
      - halving the signal (and hop) after an octave only while the hop is even (+ sqrt(2) scaling,
        resample(scale=True)); early downsampling is computed and is 0 for hop 8820
      - stacking, trimming to the shortest octave, dividing by sqrt(lengths) (scale=True)
    The only deliberate difference: the 2:1 decimations use our zero-phase Kaiser-sinc resampler
    instead of soxr 'soxr_hq'. Verified against librosa by the golden test (tests/golden).
    Not realtime-safe (allocates); analysis thread only. */
class ConstantQ
{
public:
    struct Config
    {
        double sampleRate = 44100.0;
        int hopLength = 8820;
        int numBins = 105;
        int binsPerOctave = 24;
        double fmin = 65.0;
        double filterScale = 1.0;
        double sparsity = 0.01;
    };

    /** librosa.cqt(sr=44100, hop_length=8820, n_bins=105, bins_per_octave=24, fmin=65). */
    static Config openKeyScan() { return {}; }

    explicit ConstantQ (Config);

    const Config& config() const noexcept { return cfg; }
    double binFrequency (int bin) const { return freqs[(size_t) bin]; }
    double filterLength (int bin) const { return lengths[(size_t) bin]; } // samples at cfg.sampleRate

    /** |CQT| of mono audio at cfg.sampleRate; frames = 1 + n / hop (centred). */
    CqtMatrix magnitude (const float* mono, int numSamples) const;

private:
    struct SparseRow
    {
        std::vector<int> bins;
        std::vector<std::complex<double>> weights;
    };

    struct Octave
    {
        int firstBin = 0, numBins = 0;
        double sampleRate = 0.0;
        int hop = 0, nfft = 0;
        int decimationsBefore = 0; // signal halvings applied before this octave
        std::vector<SparseRow> rows;
    };

    Config cfg;
    std::vector<double> freqs, alphas, lengths;
    std::vector<Octave> octaves;
};

/** OpenKeyScan model input from mono 44.1 kHz audio, exactly like upstream preprocess_audio():
    log1p(|cqt|), then `[:, 0:-2]` (drops the last two time frames). Shape (105, T - 2). */
CqtMatrix openKeyScanModelInput (const ConstantQ&, const float* mono44k, int numSamples);

} // namespace jaudios::dsp
