// fixture-render: deterministic, self-produced synthetic fixtures for the golden tests.
//
// Renders the fixtures listed in ml/tests/goldens.json from a known musical specification
// (tempo, key, progression, first downbeat) so every analysis stage has exact ground truth.
// These are stand-ins until real self-produced renders are dropped into tests/fixtures/:
// existing files are never overwritten unless --force is given.
//
//   fixture-render <outdir> [name ...] [--force]

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <iostream>
#include <vector>

namespace
{

constexpr double sr = 44100.0;
constexpr double twoPi = juce::MathConstants<double>::twoPi;

enum class Style { house, techno, pop, loop, bassStem };

struct Spec
{
    const char* name;
    Style style;
    double bpm;
    int tonic;               // pitch class 0..11
    bool minor;
    int bars;
    std::vector<int> degrees; // scale degree (0-based) per bar, cycled
    double firstDownbeatSec;  // silence before bar 1
};

const std::vector<Spec>& specs()
{
    static const std::vector<Spec> s {
        { "house_124_Amin",  Style::house,    124.0, 9, true,  32, { 0, 5, 2, 6 }, 0.0 },  // Am F C G
        { "techno_132_Fmin", Style::techno,   132.0, 5, true,  32, { 0, 0, 3, 0 }, 0.35 }, // Fm Fm Bbm Fm
        { "pop_100_Cmaj",    Style::pop,      100.0, 0, false, 24, { 0, 4, 5, 3 }, 0.0 },  // C G Am F
        { "loop_8bar_Gmaj",  Style::loop,     120.0, 7, false, 8,  { 0, 3, 5, 4 }, 0.0 },  // G C Em D
        { "stem_bass_Dmin",  Style::bassStem,  96.0, 2, true,  16, { 0, 5, 2, 6 }, 0.0 },  // Dm Bb F C
    };
    return s;
}

int scaleOffset (bool minor, int degree)
{
    static const int major[] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int naturalMinor[] = { 0, 2, 3, 5, 7, 8, 10 };
    const int oct = degree / 7;
    return 12 * oct + (minor ? naturalMinor : major)[degree % 7];
}

/** Diatonic triad on `degree` as MIDI notes, root in the octave starting at `baseMidi`. */
std::vector<int> triad (const Spec& s, int degree, int baseMidi)
{
    const int root = baseMidi + s.tonic + scaleOffset (s.minor, degree);
    return { root,
             baseMidi + s.tonic + scaleOffset (s.minor, degree + 2),
             baseMidi + s.tonic + scaleOffset (s.minor, degree + 4) };
}

double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

struct Renderer
{
    explicit Renderer (int numSamples) : left ((size_t) numSamples, 0.0f), right ((size_t) numSamples, 0.0f) {}

    std::vector<float> left, right;
    juce::Random rng { 1234 };

    void add (int i, double v, double pan = 0.0)
    {
        if (i < 0 || i >= (int) left.size())
            return;
        left[(size_t) i] += (float) (v * (1.0 - pan) );
        right[(size_t) i] += (float) (v * (1.0 + pan));
    }

    void kick (double t0, double gain)
    {
        const int start = (int) std::lround (t0 * sr);
        double phase = 0.0;
        for (int n = 0; n < (int) (0.35 * sr); ++n)
        {
            const double t = n / sr;
            const double f = 48.0 + 110.0 * std::exp (-t / 0.03);
            phase += twoPi * f / sr;
            add (start + n, gain * std::sin (phase) * std::exp (-t / 0.12));
        }
    }

    void noiseHit (double t0, double gain, double decay, bool bright, double pan)
    {
        const int start = (int) std::lround (t0 * sr);
        double prev = 0.0, lp = 0.0;
        for (int n = 0; n < (int) (decay * 6.0 * sr); ++n)
        {
            const double t = n / sr;
            const double w = rng.nextDouble() * 2.0 - 1.0;
            double v;
            if (bright) { v = w - prev; prev = w; }            // first-difference highpass
            else        { lp += 0.35 * (w - lp); v = lp; }     // darker body (clap/snare)
            add (start + n, gain * v * std::exp (-t / decay), pan);
        }
    }

    /** Band-limited-ish saw voice (sum of harmonics) with a simple AD envelope. */
    void tone (double t0, double dur, double midi, double gain, int harmonics, double attack, double release,
               double pan = 0.0)
    {
        const int start = (int) std::lround (t0 * sr);
        const int len = (int) ((dur + release) * sr);
        const double f = midiToHz (midi);
        const int maxH = std::max (1, std::min (harmonics, (int) (18000.0 / f)));
        for (int n = 0; n < len; ++n)
        {
            const double t = n / sr;
            double env = t < attack ? t / attack : 1.0;
            if (t > dur)
                env *= std::exp (-(t - dur) / (release / 4.0));
            double v = 0.0;
            for (int h = 1; h <= maxH; ++h)
                v += std::sin (twoPi * f * h * t) / h;
            add (start + n, gain * env * v, pan);
        }
    }
};

void render (const Spec& s, Renderer& r)
{
    const double beat = 60.0 / s.bpm;
    const double bar = 4.0 * beat;
    const double t0 = s.firstDownbeatSec;

    for (int b = 0; b < s.bars; ++b)
    {
        const int degree = s.degrees[(size_t) b % s.degrees.size()];
        const double barStart = t0 + b * bar;
        const auto chord = triad (s, degree, 60);                 // around C4..B4
        const int bassRoot = 36 + s.tonic + scaleOffset (s.minor, degree) % 12; // C2..B2 octave
        const bool drums = s.style != Style::bassStem;

        for (int q = 0; q < 4; ++q)
        {
            const double tb = barStart + q * beat;

            if (drums)
            {
                const bool fourOnFloor = s.style != Style::pop;
                if (fourOnFloor || q == 0 || q == 2)
                    r.kick (tb, q == 0 ? 0.9 : 0.8);
                if (q == 1 || q == 3)
                    r.noiseHit (tb, 0.30, 0.05, false, 0.0);

                if (s.style == Style::techno)
                    for (int x = 0; x < 4; ++x)
                        r.noiseHit (tb + x * beat / 4.0, x == 2 ? 0.12 : 0.06, 0.012, true, 0.2);
                else
                    r.noiseHit (tb + beat / 2.0, 0.12, s.style == Style::house ? 0.05 : 0.02, true, 0.2);
            }

            // Bass
            switch (s.style)
            {
                case Style::house:
                    r.tone (tb + beat / 2.0, beat * 0.4, bassRoot, 0.22, 8, 0.004, 0.03);
                    break;
                case Style::techno:
                    for (int x = 1; x < 4; ++x)
                        r.tone (tb + x * beat / 4.0, beat * 0.18, bassRoot, 0.18, 6, 0.003, 0.02);
                    break;
                case Style::pop:
                case Style::loop:
                    r.tone (tb, beat * 0.9, bassRoot, 0.22, 8, 0.005, 0.04);
                    break;
                case Style::bassStem:
                {
                    // Root on 1, fifth on 3, approach notes from the scale on 2 and 4.
                    static const int pattern[] = { 0, 2, 4, 1 };
                    const int note = 36 + s.tonic + scaleOffset (s.minor, degree + pattern[q]);
                    r.tone (tb, beat * 0.85, note, 0.30, 10, 0.005, 0.05);
                    break;
                }
            }
        }

        // Harmony
        switch (s.style)
        {
            case Style::house: // offbeat-ish stabs: "and" of 1, 2 and 4
                for (double pos : { 0.5, 1.5, 3.5 })
                    for (size_t v = 0; v < chord.size(); ++v)
                        r.tone (barStart + pos * beat, beat * 0.3, chord[v], 0.06, 12, 0.005, 0.08,
                                (double) v * 0.2 - 0.2);
                break;
            case Style::techno: // dark pad, root + fifth + minor third, held
                for (size_t v = 0; v < chord.size(); ++v)
                    r.tone (barStart, bar * 0.95, chord[v] - 12, 0.035, 6, 0.3, 0.3, (double) v * 0.3 - 0.3);
                break;
            case Style::pop:
            case Style::loop: // sustained chord + simple arpeggio on 8ths
                for (size_t v = 0; v < chord.size(); ++v)
                    r.tone (barStart, bar * 0.95, chord[v], 0.04, 8, 0.05, 0.2, (double) v * 0.2 - 0.2);
                for (int e = 0; e < 8; ++e)
                    r.tone (barStart + e * beat / 2.0, beat * 0.4, chord[(size_t) e % 3] + 12, 0.035, 4, 0.003,
                            0.05, 0.3);
                break;
            case Style::bassStem:
                break;
        }
    }
}

bool writeWav (const juce::File& file, const Renderer& r)
{
    float peak = 1.0e-9f;
    for (size_t i = 0; i < r.left.size(); ++i)
        peak = std::max ({ peak, std::abs (r.left[i]), std::abs (r.right[i]) });

    const float norm = std::pow (10.0f, -1.0f / 20.0f) / peak; // -1 dBFS peak

    juce::AudioBuffer<float> buffer (2, (int) r.left.size());
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        buffer.setSample (0, i, r.left[(size_t) i] * norm);
        buffer.setSample (1, i, r.right[(size_t) i] * norm);
    }

    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (sr)
                                                   .withNumChannels (2)
                                                   .withBitsPerSample (24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

const char* pcName (int pc, bool minor)
{
    static const char* majorNames[] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static const char* minorNames[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B" };
    return (minor ? minorNames : majorNames)[pc];
}

void writeTruth (const juce::File& file, const Spec& s)
{
    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty ("synthetic", true);
    obj->setProperty ("generator", "tools/fixtures/FixtureRender.cpp");
    obj->setProperty ("bpm", s.bpm);
    obj->setProperty ("key", juce::String (pcName (s.tonic, s.minor)) + (s.minor ? " minor" : " major"));
    obj->setProperty ("first_downbeat_sec", s.firstDownbeatSec);
    obj->setProperty ("bars", s.bars);

    juce::Array<juce::var> chords;
    for (int b = 0; b < s.bars; ++b)
    {
        const int degree = s.degrees[(size_t) b % s.degrees.size()];
        const auto t = triad (s, degree, 60);
        const bool minorTriad = ((t[1] - t[0]) == 3);
        const bool dim = minorTriad && (t[2] - t[0]) == 6;
        chords.add (juce::String (pcName (((t[0] % 12) + 12) % 12, minorTriad)) + (dim ? "dim" : minorTriad ? "m" : ""));
    }
    obj->setProperty ("chords_per_bar", chords);

    file.replaceWithText (juce::JSON::toString (juce::var (obj.release()), false) + "\n");
}

} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: fixture-render <outdir> [name ...] [--force]\n";
        return 2;
    }

    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    bool force = false;
    juce::StringArray wanted;

    for (int i = 2; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--force") force = true;
        else wanted.add (a);
    }

    int failures = 0;
    for (const auto& s : specs())
    {
        if (! wanted.isEmpty() && ! wanted.contains (s.name))
            continue;

        const auto wavFile = outDir.getChildFile (juce::String (s.name) + ".wav");
        if (wavFile.existsAsFile() && ! force)
        {
            std::cout << "keep    " << wavFile.getFullPathName() << " (exists)\n";
            continue;
        }

        const double beat = 60.0 / s.bpm;
        const int numSamples = (int) std::ceil ((s.firstDownbeatSec + s.bars * 4.0 * beat + 0.5) * sr);
        Renderer r (numSamples);
        render (s, r);

        if (! writeWav (wavFile, r))
        {
            std::cerr << "FAILED  " << wavFile.getFullPathName() << "\n";
            ++failures;
            continue;
        }

        writeTruth (outDir.getChildFile (juce::String (s.name) + ".truth.json"), s);
        std::cout << "render  " << wavFile.getFullPathName() << "  (" << juce::String (numSamples / sr, 1)
                  << " s, synthetic)\n";
    }

    return failures == 0 ? 0 : 1;
}
