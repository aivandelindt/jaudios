#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <memory>

namespace jaudios::test
{

inline std::shared_ptr<juce::AudioBuffer<float>> makeSine (int numChannels, int numSamples, double freq,
                                                           double sampleRate, float amplitude = 0.5f)
{
    auto b = std::make_shared<juce::AudioBuffer<float>> (numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            b->setSample (ch, i, amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * freq * i / sampleRate
                                                               + 0.3 * ch));
    return b;
}

/** Temp directory removed on destruction. */
struct TempDir
{
    TempDir() : dir (juce::File::createTempFile ("jaudios_test")) { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
    juce::File dir;
};

} // namespace jaudios::test

#include <array>
#include <vector>

namespace jaudios::test
{

struct Note
{
    double start = 0.0, duration = 0.0, midi = 60.0, gain = 0.1;
};

/** Additive tone synthesis (1/h harmonic rolloff) with short attack/release. Mono. */
inline juce::AudioBuffer<float> renderNotes (const std::vector<Note>& notes, double totalSec, double sr = 44100.0,
                                             int harmonics = 6, double tuningA4 = 440.0)
{
    juce::AudioBuffer<float> out (1, (int) std::ceil (totalSec * sr));
    out.clear();
    auto* y = out.getWritePointer (0);
    for (const auto& n : notes)
    {
        const double f = tuningA4 * std::pow (2.0, (n.midi - 69.0) / 12.0);
        const int start = (int) std::lround (n.start * sr);
        const int len = (int) (n.duration * sr);
        for (int i = 0; i < len && start + i < out.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const double env = std::min ({ 1.0, t / 0.01, (n.duration - t) / 0.02 });
            double v = 0.0;
            for (int h = 1; h <= harmonics && f * h < sr * 0.45; ++h)
                v += std::sin (juce::MathConstants<double>::twoPi * f * h * t) / h;
            y[start + i] += (float) (n.gain * env * v);
        }
    }
    return out;
}

/** Diatonic triads for `degrees` (0-based) in a key, one chord per `chordSec`, plus bass roots. */
inline std::vector<Note> progression (int tonic, bool isMinor, const std::vector<int>& degrees, double chordSec,
                                      int repeats = 2)
{
    // Note: no function-local static tables read from a lambda, and no identifiers named
    // `major` / `minor` (glibc macros): that combination produced garbage pitches in the
    // GCC -O0 + ASan build.
    constexpr std::array<int, 7> majorScale { 0, 2, 4, 5, 7, 9, 11 };
    constexpr std::array<int, 7> harmonicMinorScale { 0, 2, 3, 5, 7, 8, 11 }; // raised 7th: V is major
    const std::array<int, 7> scale = isMinor ? harmonicMinorScale : majorScale;
    auto pitch = [scale] (int degree) { return 12 * (degree / 7) + scale[(size_t) (degree % 7)]; };

    std::vector<Note> notes;
    double t = 0.0;
    for (int r = 0; r < repeats; ++r)
        for (int d : degrees)
        {
            for (int v = 0; v < 3; ++v)
                notes.push_back ({ t, chordSec * 0.95, 60.0 + tonic + pitch (d + 2 * v), 0.08 });
            notes.push_back ({ t, chordSec * 0.95, 36.0 + tonic + pitch (d) % 12, 0.12 });
            t += chordSec;
        }
    return notes;
}

/** Click track: 1 kHz/2 kHz decaying sine bursts. Downbeats louder and lower if accentDownbeats. */
inline juce::AudioBuffer<float> clickTrack (double bpm, double offsetSec, int numBeats, bool accentDownbeats,
                                            double sr = 44100.0, double tailSec = 1.0)
{
    const double beat = 60.0 / bpm;
    juce::AudioBuffer<float> out (1, (int) std::ceil ((offsetSec + numBeats * beat + tailSec) * sr));
    out.clear();
    auto* y = out.getWritePointer (0);
    for (int b = 0; b < numBeats; ++b)
    {
        const bool down = accentDownbeats && b % 4 == 0;
        const double f = down ? 1000.0 : 2000.0;
        const double g = down ? 0.9 : 0.5;
        const int start = (int) std::lround ((offsetSec + b * beat) * sr);
        for (int i = 0; i < (int) (0.05 * sr) && start + i < out.getNumSamples(); ++i)
            y[start + i] += (float) (g * std::exp (-i / (0.008 * sr)) * std::sin (juce::MathConstants<double>::twoPi * f * i / sr));
    }
    return out;
}

/** Kick (pitch-swept sine) on every beat + offbeat noise hat; deterministic noise. */
inline void addFourOnFloor (juce::AudioBuffer<float>& buf, double bpm, double offsetSec, int numBeats, double sr = 44100.0)
{
    const double beat = 60.0 / bpm;
    auto* y = buf.getWritePointer (0);
    juce::Random rng (42);
    for (int b = 0; b < numBeats; ++b)
    {
        int start = (int) std::lround ((offsetSec + b * beat) * sr);
        double phase = 0.0;
        for (int i = 0; i < (int) (0.3 * sr) && start + i < buf.getNumSamples(); ++i)
        {
            const double t = i / sr;
            phase += juce::MathConstants<double>::twoPi * (48.0 + 110.0 * std::exp (-t / 0.03)) / sr;
            y[start + i] += (float) (0.6 * std::sin (phase) * std::exp (-t / 0.12));
        }
        start = (int) std::lround ((offsetSec + (b + 0.5) * beat) * sr);
        double prev = 0.0;
        for (int i = 0; i < (int) (0.05 * sr) && start + i < buf.getNumSamples(); ++i)
        {
            const double w = rng.nextDouble() * 2.0 - 1.0;
            y[start + i] += (float) (0.15 * (w - prev) * std::exp (-i / (0.01 * sr)));
            prev = w;
        }
    }
}

inline juce::AudioBuffer<float> prependSilence (const juce::AudioBuffer<float>& in, double sec, double sr = 44100.0)
{
    const int pad = (int) std::lround (sec * sr);
    juce::AudioBuffer<float> out (in.getNumChannels(), in.getNumSamples() + pad);
    out.clear();
    for (int ch = 0; ch < in.getNumChannels(); ++ch)
        out.copyFrom (ch, pad, in, ch, 0, in.getNumSamples());
    return out;
}

} // namespace jaudios::test
