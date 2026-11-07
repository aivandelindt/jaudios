#pragma once

#include <juce_dsp/juce_dsp.h>

#include <vector>

namespace jaudios::analysis::stems
{

/** HT-Demucs (demucs 4.1.0, htdemucs) fixed geometry: one 7.8 s segment at 44.1 kHz. */
struct DemucsGeometry
{
    static constexpr int sampleRate = 44100;
    static constexpr int segment = 343980;     // int(39/5 * 44100), HTDemucs training segment
    static constexpr int nfft = 4096;
    static constexpr int hop = 1024;           // nfft / 4
    static constexpr int bins = nfft / 2;      // 2048: the Nyquist bin is dropped
    static constexpr int frames = 336;         // ceil(segment / hop)
    static constexpr int channels = 2;
    static constexpr int sources = 4;          // drums, bass, other, vocals (model.sources)
    static constexpr int specPlane = bins * frames;
    static constexpr int specSize = channels * 2 * specPlane; // CaC: (L.re, L.im, R.re, R.im) x bins x frames
};

/** C++ port of HTDemucs._spec + _magnitude (CaC) and _mask + _ispec, i.e. demucs.spec.spectro /
    ispectro on torch.stft / torch.istft semantics:
      - reflect-pad by 3*hop/2 (+ right pad to a whole number of hops), torch.stft(center=True,
        pad_mode='reflect', periodic Hann, normalized=True), drop the Nyquist bin and 2 frames per side
      - inverse: zero Nyquist bin + 2 zero frames per side, torch.istft(normalized=True, center=True)
        with window-sum-of-squares normalisation, then trim the same padding.
    Layouts are the ONNX graph's: spectrogram (4, 2048, 336) with frames fastest, audio (2, 343980).
    Not real-time code (worker thread only); one instance per thread. */
class DemucsSpec
{
public:
    DemucsSpec();

    /** stereo: channels x segment samples (planar, normalised). spec: specSize floats. */
    void forward (const float* const* stereo, float* spec);

    /** spec: one source's CaC spectrogram (specSize floats). out: 2 planar channels x segment;
        the time-domain result is ADDED to out (the separator adds it to the time branch). */
    void inverseAdd (const float* spec, float* const* out);

private:
    using G = DemucsGeometry;
    static constexpr int padSpec = G::hop / 2 * 3;               // 1536 (_spec / _ispec)
    static constexpr int paddedLength = G::frames * G::hop + 2 * padSpec; // 347136
    static constexpr int stftFrames = G::frames + 4;             // 340 (2 dropped per side)

    juce::dsp::FFT fft { 12 };
    std::vector<float> window, fftBuffer, padded, ola, envelopeInv;
};

} // namespace jaudios::analysis::stems
