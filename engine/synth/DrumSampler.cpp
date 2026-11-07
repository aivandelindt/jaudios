#include "DrumSampler.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <map>

namespace jaudios::synth
{

using generators::DrumNote;

namespace
{
constexpr double twoPi = 6.283185307179586;
constexpr int numLayers = 3, numRoundRobins = 3;

/** Simple deterministic noise source for the synthesised kit. */
struct Noise
{
    juce::Random rng;
    explicit Noise (juce::int64 seed) : rng (seed) {}
    float next() { return rng.nextFloat() * 2.0f - 1.0f; }
};

void normalise (juce::AudioBuffer<float>& b, float peak)
{
    const float m = b.getMagnitude (0, 0, b.getNumSamples());
    if (m > 0.0f)
        b.applyGain (peak / m);
}

/** One synthesised hit. layer 0..2 (soft .. hard: brighter, more attack), rr 0..2 (noise seed + pitch). */
juce::AudioBuffer<float> synthesise (DrumElement el, int layer, int rr, double sr)
{
    const double bright = 0.6 + 0.2 * layer;                 // 0.6 .. 1.0
    const double detune = 1.0 + 0.015 * (rr - 1);            // -1.5 % .. +1.5 %
    Noise noise (1000 + 97 * (int) el + 13 * layer + rr);
    auto make = [&] (double seconds) { juce::AudioBuffer<float> b (1, (int) (seconds * sr)); b.clear(); return b; };
    juce::AudioBuffer<float> b;
    double phase = 0.0, lp = 0.0, prev = 0.0;

    switch (el)
    {
        case DrumElement::kick:
        {
            b = make (0.5);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = i / sr;
                const double f = (48.0 + 120.0 * bright * std::exp (-t / 0.035)) * detune;
                // cosine start: the first sample is the attack's peak, so the onset is sample-accurate
                const double body = std::cos (phase) * std::exp (-t / 0.22);
                const double click = t < 0.003 ? 0.35 * bright * noise.next() * (1.0 - t / 0.003) : 0.0;
                b.setSample (0, i, (float) (body + click));
                phase += twoPi * f / sr;
            }
            break;
        }
        case DrumElement::snare:
        {
            b = make (0.35);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = i / sr;
                const double tone = 0.6 * std::cos (phase) * std::exp (-t / 0.07);
                phase += twoPi * 190.0 * detune / sr;
                const double w = noise.next();
                const double hp = w - prev; prev = w;
                lp += (0.25 + 0.35 * bright) * (hp - lp);
                b.setSample (0, i, (float) (tone + 0.9 * lp * std::exp (-t / (0.08 + 0.04 * bright))));
            }
            break;
        }
        case DrumElement::clap:
        {
            b = make (0.4);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = i / sr;
                double env = std::exp (-t / 0.11) * 0.6;
                for (double burst : { 0.0, 0.011, 0.022 })
                    if (t >= burst)
                        env += std::exp (-(t - burst) / 0.004);
                const double w = noise.next();
                lp += 0.45 * (w - prev - lp); prev = w;
                b.setSample (0, i, (float) (lp * env * bright));
            }
            break;
        }
        case DrumElement::hat:
        case DrumElement::openhat:
        {
            const bool open = el == DrumElement::openhat;
            b = make (open ? 0.6 : 0.12);
            double prev2 = 0.0;
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = i / sr;
                const double w = noise.next();
                const double hp = w - 2.0 * prev + prev2; prev2 = prev; prev = w; // 2nd difference: bright
                b.setSample (0, i, (float) (hp * std::exp (-t / ((open ? 0.22 : 0.03) * (0.8 + 0.2 * bright)))));
            }
            break;
        }
        case DrumElement::perc:
        {
            b = make (0.25);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = i / sr;
                const double f = 320.0 * detune;
                const double v = std::cos (twoPi * f * t) + 0.5 * bright * std::cos (twoPi * 2.71 * f * t);
                b.setSample (0, i, (float) (v * std::exp (-t / 0.055) + (t < 0.002 ? 0.3 * noise.next() : 0.0)));
            }
            break;
        }
        case DrumElement::ride:
        {
            b = make (1.5);
            static const double partials[] = { 1.0, 1.47, 2.09, 2.56, 3.17, 3.91 };
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const double t = i / sr;
                double v = 0.0;
                for (size_t k = 0; k < 6; ++k)
                    v += std::cos (twoPi * 430.0 * detune * partials[k] * t) / (1.0 + (double) k * (1.2 - bright));
                const double w = noise.next();
                const double hp = w - prev; prev = w;
                b.setSample (0, i, (float) (0.25 * v * std::exp (-t / 0.7) + 0.4 * hp * std::exp (-t / 0.25)));
            }
            break;
        }
    }
    normalise (b, (float) (0.5 + 0.2 * layer));
    return b;
}

/** Fixed stereo position per element (-1 left .. 1 right). */
float panOf (DrumElement e)
{
    switch (e)
    {
        case DrumElement::hat: return 0.25f;
        case DrumElement::openhat: return 0.3f;
        case DrumElement::perc: return -0.35f;
        case DrumElement::ride: return -0.25f;
        case DrumElement::clap: return 0.05f;
        default: return 0.0f;
    }
}

struct DrumSound final : juce::SynthesiserSound
{
    DrumSound (DrumElement e, const std::vector<std::vector<juce::AudioBuffer<float>>>& l) : element (e), layers (l) {}
    bool appliesToNote (int note) override { return note == generators::gmNote (element); }
    bool appliesToChannel (int) override { return true; }

    DrumElement element;
    const std::vector<std::vector<juce::AudioBuffer<float>>>& layers; // owned by the sampler's kit copy
    std::atomic<unsigned> roundRobin { 0 };
};

struct DrumVoice final : juce::SynthesiserVoice
{
    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<DrumSound*> (s) != nullptr; }

    void startNote (int, float velocity, juce::SynthesiserSound* s, int) override
    {
        auto* sound = static_cast<DrumSound*> (s);
        sample = nullptr;
        if (sound->layers.empty())
            return;
        const int layer = std::min ((int) sound->layers.size() - 1, (int) (velocity * 3.0f * 0.999f)); // velocity thirds
        const auto& rrs = sound->layers[(size_t) layer];
        if (rrs.empty())
            return;
        sample = &rrs[(size_t) (sound->roundRobin.fetch_add (1, std::memory_order_relaxed) % rrs.size())];
        position = 0;
        fade = -1;
        const float gain = 0.35f + 0.65f * velocity;
        const float pan = panOf (sound->element);
        gainL = gain * std::sqrt (0.5f * (1.0f - pan));
        gainR = gain * std::sqrt (0.5f * (1.0f + pan));
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (! allowTailOff && sample != nullptr) // choke: 64-sample fade
            fade = 64;
        // note-offs with tail-off are ignored: drums are one-shots
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (sample == nullptr)
            return;
        const float* src = sample->getReadPointer (0);
        const int len = sample->getNumSamples();
        for (int i = 0; i < num && position < len; ++i, ++position)
        {
            float v = src[position];
            if (fade >= 0)
            {
                v *= (float) fade / 64.0f;
                if (--fade < 0)
                {
                    position = len;
                    break;
                }
            }
            out.addSample (0, start + i, v * gainL);
            if (out.getNumChannels() > 1)
                out.addSample (1, start + i, v * gainR);
        }
        if (position >= len)
        {
            sample = nullptr;
            clearCurrentNote();
        }
    }

    const juce::AudioBuffer<float>* sample = nullptr;
    int position = 0, fade = -1;
    float gainL = 0.7f, gainR = 0.7f;
};
} // namespace

DrumKit DrumKit::synthesised (double sr)
{
    DrumKit kit;
    kit.sampleRate = sr;
    kit.name = "JAudios synthesised";
    for (int e = 0; e < numDrumElements; ++e)
    {
        kit.layers[(size_t) e].resize (numLayers);
        for (int l = 0; l < numLayers; ++l)
            for (int r = 0; r < numRoundRobins; ++r)
                kit.layers[(size_t) e][(size_t) l].push_back (synthesise ((DrumElement) e, l, r, sr));
    }
    return kit;
}

std::optional<DrumKit> DrumKit::loadFolder (const juce::File& dir, double sr, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::array<std::map<int, std::map<int, juce::AudioBuffer<float>>>, numDrumElements> found;
    int files = 0;
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac"))
    {
        // <element>[_v<layer>][_rr<n>]
        juce::StringArray parts;
        parts.addTokens (f.getFileNameWithoutExtension().toLowerCase(), "_", {});
        const auto el = parts.isEmpty() ? std::nullopt : generators::elementFromName (parts[0]);
        if (! el)
            continue;
        int layer = 1, rr = 1;
        bool ok = true;
        for (int i = 1; i < parts.size() && ok; ++i)
        {
            if (parts[i].startsWith ("v") && parts[i].substring (1).containsOnly ("0123456789") && parts[i].length() > 1)
                layer = parts[i].substring (1).getIntValue();
            else if (parts[i].startsWith ("rr") && parts[i].substring (2).containsOnly ("0123456789") && parts[i].length() > 2)
                rr = parts[i].substring (2).getIntValue();
            else
                ok = false;
        }
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        if (! ok || reader == nullptr || reader->lengthInSamples <= 0)
            continue;
        juce::AudioBuffer<float> raw ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&raw, 0, raw.getNumSamples(), 0, true, true);
        juce::AudioBuffer<float> mono (1, raw.getNumSamples());
        mono.clear();
        for (int ch = 0; ch < raw.getNumChannels(); ++ch)
            mono.addFrom (0, 0, raw, ch, 0, raw.getNumSamples(), 1.0f / (float) raw.getNumChannels());
        if (std::abs (reader->sampleRate - sr) > 0.5)
        {
            const double ratio = reader->sampleRate / sr;
            juce::AudioBuffer<float> rs (1, (int) std::ceil (mono.getNumSamples() / ratio));
            juce::LagrangeInterpolator interp;
            interp.process (ratio, mono.getReadPointer (0), rs.getWritePointer (0), rs.getNumSamples(), mono.getNumSamples(), 0);
            mono = std::move (rs);
        }
        found[(size_t) *el][layer][rr] = std::move (mono);
        ++files;
    }
    if (files == 0)
    {
        error = "no <element>[_v<n>][_rr<n>].wav files in " + dir.getFullPathName()
              + " (elements: kick snare clap hat openhat perc ride)";
        return std::nullopt;
    }
    DrumKit kit = synthesised (sr);
    kit.name = dir.getFileName();
    for (int e = 0; e < numDrumElements; ++e)
    {
        if (found[(size_t) e].empty())
            continue; // keep the synthesised element
        auto& layers = kit.layers[(size_t) e];
        layers.clear();
        for (auto& [layer, rrs] : found[(size_t) e])
        {
            layers.emplace_back();
            for (auto& [rr, buf] : rrs)
                layers.back().push_back (std::move (buf));
        }
    }
    return kit;
}

//==============================================================================
DrumSampler::DrumSampler (const DrumKit& kit, int numVoices)
{
    for (int e = 0; e < numDrumElements; ++e)
        addSound (new DrumSound ((DrumElement) e, kit.layers[(size_t) e]));
    for (int v = 0; v < numVoices; ++v)
        addVoice (new DrumVoice());
    setNoteStealingEnabled (true);
    setCurrentPlaybackSampleRate (kit.sampleRate);
}

void DrumSampler::noteOn (int channel, int note, float velocity)
{
    if (soloElement && note != generators::gmNote (*soloElement))
        return;
    if (note == generators::gmNote (DrumElement::hat)) // closed hat chokes the open hat
        for (auto* v : voices)
            if (v->getCurrentlyPlayingNote() == generators::gmNote (DrumElement::openhat))
                v->stopNote (0.0f, false);
    juce::Synthesiser::noteOn (channel, note, velocity);
}

juce::AudioBuffer<float> renderDrums (const std::vector<DrumNote>& notes, const DrumKit& kit, double sr, int numSamples,
                                      std::optional<DrumElement> only)
{
    juce::AudioBuffer<float> out (2, std::max (0, numSamples));
    out.clear();
    if (numSamples <= 0)
        return out;
    jassert (std::abs (kit.sampleRate - sr) < 0.5);
    DrumSampler sampler (kit);
    sampler.solo (only);

    struct Event { int sample; juce::MidiMessage msg; };
    std::vector<Event> events;
    for (const auto& n : notes)
    {
        const int note = generators::gmNote (n.element);
        events.push_back ({ (int) std::lround (n.startSec * sr), juce::MidiMessage::noteOn (10, note, (juce::uint8) n.velocity) });
        events.push_back ({ (int) std::lround (n.endSec * sr), juce::MidiMessage::noteOff (10, note) });
    }
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.sample < b.sample; });

    constexpr int block = 512;
    size_t next = 0;
    juce::MidiBuffer midi;
    for (int pos = 0; pos < numSamples; pos += block)
    {
        const int len = std::min (block, numSamples - pos);
        midi.clear();
        // Synthesiser::renderNextBlock reads event positions in the output buffer's coordinates
        while (next < events.size() && events[next].sample < pos + len)
        {
            midi.addEvent (events[next].msg, std::max (pos, events[next].sample));
            ++next;
        }
        sampler.renderNextBlock (out, midi, pos, len);
    }
    return out;
}

juce::MidiMessageSequence drumsToMidi (const std::vector<DrumNote>& notes, std::optional<DrumElement> only)
{
    juce::MidiMessageSequence seq;
    for (const auto& n : notes)
    {
        if (only && n.element != *only)
            continue;
        const int note = generators::gmNote (n.element);
        seq.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) n.velocity).withTimeStamp (n.startSec));
        seq.addEvent (juce::MidiMessage::noteOff (10, note).withTimeStamp (n.endSec));
    }
    seq.sort();
    seq.updateMatchedPairs();
    return seq;
}

} // namespace jaudios::synth
