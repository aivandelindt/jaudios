#include "DemucsSpec.h"

#include <cmath>

namespace jaudios::analysis::stems
{

namespace
{
/** torch F.pad(mode='reflect') index: mirror without repeating the edge sample. */
inline int reflect (int i, int n) noexcept
{
    while (i < 0 || i >= n)
        i = i < 0 ? -i : 2 * (n - 1) - i;
    return i;
}
} // namespace

DemucsSpec::DemucsSpec()
    : window ((size_t) G::nfft), fftBuffer ((size_t) G::nfft * 2), padded ((size_t) paddedLength),
      ola ((size_t) G::segment + 2 * G::nfft), envelopeInv ((size_t) G::segment)
{
    // torch.hann_window(n_fft): periodic
    for (int n = 0; n < G::nfft; ++n)
        window[(size_t) n] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * n / G::nfft));

    // istft window envelope over all 340 torch frames (incl. the zero-padded ones), in output
    // coordinates: torch frame t starts at t*hop - nfft/2 - padSpec.
    std::vector<double> env ((size_t) G::segment, 0.0);
    for (int t = 0; t < stftFrames; ++t)
    {
        const int start = t * G::hop - G::nfft / 2 - padSpec;
        for (int n = 0; n < G::nfft; ++n)
            if (const int j = start + n; j >= 0 && j < G::segment)
                env[(size_t) j] += (double) window[(size_t) n] * window[(size_t) n];
    }
    for (int j = 0; j < G::segment; ++j)
        envelopeInv[(size_t) j] = (float) (1.0 / env[(size_t) j]);
}

void DemucsSpec::forward (const float* const* stereo, float* spec)
{
    // _spec: reflect pad (padSpec, padSpec + frames*hop - segment). torch.stft's own centre padding
    // only feeds the 2 frames per side that _spec drops, so kept frame k is padded[k*hop, k*hop+nfft).
    const float scale = 1.0f / std::sqrt ((float) G::nfft); // normalized=True
    for (int ch = 0; ch < G::channels; ++ch)
    {
        const float* x = stereo[ch];
        for (int i = 0; i < paddedLength; ++i)
            padded[(size_t) i] = x[reflect (i - padSpec, G::segment)];

        float* re = spec + (size_t) (2 * ch) * G::specPlane;
        float* im = spec + (size_t) (2 * ch + 1) * G::specPlane;
        for (int k = 0; k < G::frames; ++k)
        {
            const float* frame = padded.data() + (size_t) k * G::hop;
            for (int n = 0; n < G::nfft; ++n)
                fftBuffer[(size_t) n] = frame[n] * window[(size_t) n];
            fft.performRealOnlyForwardTransform (fftBuffer.data(), true); // interleaved bins 0..nfft/2

            for (int f = 0; f < G::bins; ++f)
            {
                re[(size_t) f * G::frames + (size_t) k] = fftBuffer[(size_t) (2 * f)] * scale;
                im[(size_t) f * G::frames + (size_t) k] = fftBuffer[(size_t) (2 * f + 1)] * scale;
            }
        }
    }
}

void DemucsSpec::inverseAdd (const float* spec, float* const* out)
{
    // juce::dsp::FFT's inverse already divides by nfft (like irfft); normalized=True multiplies the
    // spectrum by sqrt(nfft) first.
    const float scale = std::sqrt ((float) G::nfft);
    for (int ch = 0; ch < G::channels; ++ch)
    {
        const float* re = spec + (size_t) (2 * ch) * G::specPlane;
        const float* im = spec + (size_t) (2 * ch + 1) * G::specPlane;
        std::fill (ola.begin(), ola.end(), 0.0f);

        // kept frame k = torch frame k+2: starts at (k+2)*hop - nfft/2 - padSpec in output
        // coordinates; ola is offset by nfft so negative starts stay in range.
        for (int k = 0; k < G::frames; ++k)
        {
            for (int f = 0; f < G::bins; ++f)
            {
                fftBuffer[(size_t) (2 * f)] = re[(size_t) f * G::frames + (size_t) k] * scale;
                fftBuffer[(size_t) (2 * f + 1)] = im[(size_t) f * G::frames + (size_t) k] * scale;
            }
            fftBuffer[(size_t) (2 * G::bins)] = 0.0f; // Nyquist bin re-inserted as zero
            fftBuffer[(size_t) (2 * G::bins + 1)] = 0.0f;
            fft.performRealOnlyInverseTransform (fftBuffer.data());

            float* dst = ola.data() + (size_t) (G::nfft + (k + 2) * G::hop - G::nfft / 2 - padSpec);
            for (int n = 0; n < G::nfft; ++n)
                dst[n] += fftBuffer[(size_t) n] * window[(size_t) n];
        }

        float* y = out[ch];
        const float* src = ola.data() + G::nfft;
        for (int j = 0; j < G::segment; ++j)
            y[j] += src[j] * envelopeInv[(size_t) j];
    }
}

} // namespace jaudios::analysis::stems
