#include "FxChain.h"

#include <cmath>

namespace jaudios::fx
{

namespace
{
using K = ParamSpec::Kind;

std::array<ParamSpec, numParams> makeSpecs()
{
    return { {
        { Param::filterOn,        "filter_on",        "Filter",              K::toggle, 0, 1, 0, 0, "", {} },
        { Param::filterType,      "filter_type",      "Filter type",         K::choice, 0, 2, 0, 0, "", { "Low-pass", "High-pass", "Band-pass" } },
        { Param::filterCutoff,    "filter_cutoff",    "Filter cutoff",       K::range, 20.0f, 20000.0f, 20000.0f, 640.0f, "Hz", {} },
        { Param::filterResonance, "filter_resonance", "Filter resonance",    K::range, 0.3f, 8.0f, 0.7071f, 1.5f, "", {} },
        { Param::satOn,           "sat_on",           "Saturation",          K::toggle, 0, 1, 0, 0, "", {} },
        { Param::satDrive,        "sat_drive",        "Saturation drive",    K::range, 0.0f, 24.0f, 6.0f, 0, "dB", {} },
        { Param::satMix,          "sat_mix",          "Saturation mix",      K::range, 0.0f, 1.0f, 1.0f, 0, "", {} },
        { Param::ottOn,           "ott_on",           "OTT",                 K::toggle, 0, 1, 0, 0, "", {} },
        { Param::ottDepth,        "ott_depth",        "OTT depth",           K::range, 0.0f, 1.0f, 0.5f, 0, "", {} },
        { Param::ottTime,         "ott_time",         "OTT time",            K::range, 0.1f, 10.0f, 1.0f, 1.0f, "x", {} },
        { Param::ottInGain,       "ott_in_gain",      "OTT input gain",      K::range, -24.0f, 24.0f, 0.0f, 0, "dB", {} },
        { Param::ottOutGain,      "ott_out_gain",     "OTT output gain",     K::range, -24.0f, 24.0f, 0.0f, 0, "dB", {} },
        { Param::duckOn,          "duck_on",          "Sidechain duck",      K::toggle, 0, 1, 0, 0, "", {} },
        { Param::duckSource,      "duck_source",      "Duck key",            K::choice, 0, 1, 0, 0, "", { "Kick events", "Drums stem" } },
        { Param::duckDepth,       "duck_depth",       "Duck depth",          K::range, 0.0f, 1.0f, 0.6f, 0, "", {} },
        { Param::duckRelease,     "duck_release",     "Duck release",        K::range, 20.0f, 1000.0f, 180.0f, 180.0f, "ms", {} },
        { Param::delayOn,         "delay_on",         "Delay",               K::toggle, 0, 1, 0, 0, "", {} },
        { Param::delayDivision,   "delay_division",   "Delay time",          K::choice, 0, 5, 2, 0, "", { "1/16", "1/8", "1/8 dotted", "1/4", "1/4 dotted", "1/2" } },
        { Param::delayFeedback,   "delay_feedback",   "Delay feedback",      K::range, 0.0f, 0.95f, 0.35f, 0, "", {} },
        { Param::delayMix,        "delay_mix",        "Delay mix",           K::range, 0.0f, 1.0f, 0.25f, 0, "", {} },
        { Param::reverbOn,        "reverb_on",        "Reverb",              K::toggle, 0, 1, 0, 0, "", {} },
        { Param::reverbSize,      "reverb_size",      "Reverb size",         K::range, 0.0f, 1.0f, 0.6f, 0, "", {} },
        { Param::reverbDamping,   "reverb_damping",   "Reverb damping",      K::range, 0.0f, 1.0f, 0.4f, 0, "", {} },
        { Param::reverbWidth,     "reverb_width",     "Reverb width",        K::range, 0.0f, 1.0f, 1.0f, 0, "", {} },
        { Param::reverbMix,       "reverb_mix",       "Reverb mix",          K::range, 0.0f, 1.0f, 0.2f, 0, "", {} },
    } };
}

constexpr double switchSeconds = 0.01;  // stage on/off crossfade
constexpr double smoothSeconds = 0.03;  // continuous parameters
constexpr int tapFadeMs = 50;           // delay-time change crossfade

// OTT-style compressor, per band (low, mid, high): attack / release ms at time 1.0
constexpr std::array<float, 3> ottAttackMs { 47.8f, 22.4f, 13.5f };
constexpr std::array<float, 3> ottReleaseMs { 282.0f, 282.0f, 132.0f };
constexpr float ottDownThresholdDb = -30.0f, ottDownRatio = 8.0f;
constexpr float ottUpThresholdDb = -30.0f, ottUpRatio = 3.0f, ottMaxUpDb = 24.0f;
constexpr float ottFloorDb = -80.0f, ottFloorRampDb = 12.0f; // no upward gain on silence

float coefficient (double sampleRate, float ms) noexcept
{
    return std::exp (-1.0f / (float) (sampleRate * ms * 0.001));
}
} // namespace

const std::array<ParamSpec, numParams>& paramSpecs()
{
    static const auto specs = makeSpecs();
    return specs;
}

const ParamSpec& spec (Param p) { return paramSpecs()[(size_t) p]; }

const ParamSpec* findSpec (juce::StringRef id)
{
    for (const auto& s : paramSpecs())
        if (id == juce::StringRef (s.id))
            return &s;
    return nullptr;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& s : paramSpecs())
    {
        const juce::ParameterID id { s.id, 1 };
        switch (s.kind)
        {
            case K::toggle:
                layout.add (std::make_unique<juce::AudioParameterBool> (id, s.name, s.defaultValue >= 0.5f));
                break;
            case K::choice:
            {
                juce::StringArray choices;
                for (auto* c : s.choices)
                    choices.add (c);
                layout.add (std::make_unique<juce::AudioParameterChoice> (id, s.name, choices, (int) s.defaultValue));
                break;
            }
            case K::range:
            {
                juce::NormalisableRange<float> range (s.min, s.max);
                if (s.skewCentre > 0.0f)
                    range.setSkewForCentre (s.skewCentre);
                layout.add (std::make_unique<juce::AudioParameterFloat> (id, s.name, range, s.defaultValue,
                                                                         juce::AudioParameterFloatAttributes().withLabel (s.unit)));
                break;
            }
        }
    }
    return layout;
}

bool setParameter (juce::AudioProcessorValueTreeState& state, juce::StringRef id, float plainValue)
{
    auto* p = state.getParameter (id);
    const auto* s = findSpec (id);
    if (p == nullptr || s == nullptr)
        return false;
    p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (s->min, s->max, plainValue)));
    return true;
}

bool setParameter (juce::AudioProcessorValueTreeState& state, juce::StringRef id, const juce::String& text)
{
    const auto* s = findSpec (id);
    if (s == nullptr)
        return false;
    if (s->kind == K::choice)
        for (size_t i = 0; i < s->choices.size(); ++i)
            if (text.equalsIgnoreCase (s->choices[i]))
                return setParameter (state, id, (float) i);
    if (s->kind == K::toggle && (text.equalsIgnoreCase ("on") || text.equalsIgnoreCase ("off")))
        return setParameter (state, id, text.equalsIgnoreCase ("on") ? 1.0f : 0.0f);
    if (! text.trim().containsOnly ("-+.0123456789eE") || text.trim().isEmpty())
        return false;
    return setParameter (state, id, text.getFloatValue());
}

ParameterHost::ParameterHost()
    : juce::AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FX", createParameterLayout())
{
}

//==============================================================================
bool FxChain::Switch::update (bool shouldBeOn, bool& justStarted) noexcept
{
    justStarted = shouldBeOn && amount.getCurrentValue() <= 0.0f && ! amount.isSmoothing();
    amount.setTargetValue (shouldBeOn ? 1.0f : 0.0f);
    return shouldBeOn || amount.isSmoothing() || amount.getCurrentValue() > 0.0f;
}

FxChain::FxChain() = default;

void FxChain::bind (juce::AudioProcessorValueTreeState& state)
{
    for (const auto& s : paramSpecs())
        values[(size_t) s.param] = state.getRawParameterValue (s.id);
}

void FxChain::setTempo (double bpm) noexcept
{
    tempo.store ((float) juce::jlimit (30.0, 300.0, bpm), std::memory_order_relaxed);
}

void FxChain::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr;
    maxBlock = maxBlockSize;
    for (auto* sw : { &filterSwitch, &satSwitch, &ottSwitch, &duckSwitch, &delaySwitch, &reverbSwitch })
    {
        sw->amount.reset (sr, switchSeconds);
        sw->amount.setCurrentAndTargetValue (0.0f);
    }
    auto init = [sr] (auto& smoothed, float value) { smoothed.reset (sr, smoothSeconds); smoothed.setCurrentAndTargetValue (value); };
    init (cutoff, raw (Param::filterCutoff));
    init (resonance, raw (Param::filterResonance));
    for (size_t t = 0; t < typeWeights.size(); ++t)
        init (typeWeights[t], (int) raw (Param::filterType) == (int) t ? 1.0f : 0.0f);
    init (drive, juce::Decibels::decibelsToGain (raw (Param::satDrive)));
    init (satMix, raw (Param::satMix));
    init (ottDepth, raw (Param::ottDepth));
    init (ottTime, raw (Param::ottTime));
    init (ottIn, juce::Decibels::decibelsToGain (raw (Param::ottInGain)));
    init (ottOut, juce::Decibels::decibelsToGain (raw (Param::ottOutGain)));
    init (duckDepth, raw (Param::duckDepth));
    init (duckRelease, raw (Param::duckRelease));
    init (feedback, raw (Param::delayFeedback));
    init (delayMix, raw (Param::delayMix));
    init (reverbMix, raw (Param::reverbMix));

    const juce::dsp::ProcessSpec specs { sr, (juce::uint32) maxBlockSize, 2 };
    for (auto* f : { &crossLow, &crossHigh, &lowAllpass })
        f->prepare (specs);
    crossLow.setCutoffFrequency (88.3f);
    crossHigh.setCutoffFrequency (2500.0f);
    lowAllpass.setCutoffFrequency (2500.0f);
    ottEnv.fill (0.0f);
    ic1.fill (0.0f);
    ic2.fill (0.0f);
    duckEnv = 0.0f;
    duckGain = 1.0f;

    // the longest synced time: 2 beats at 30 bpm = 4 s
    delayLines.setSize (2, (int) std::ceil (4.0 * sr) + 2, false, true, false);
    writePos = 0;
    delayNow = delayNext = 0;
    tapFade = 1.0f;

    reverbEngine.setSampleRate (sr);
    reverbEngine.reset();
    scratch.setSize (3, maxBlockSize, false, true, false); // reverb L / R + the switch ramp
}

bool FxChain::isActive() const noexcept
{
    for (auto p : { Param::filterOn, Param::satOn, Param::ottOn, Param::duckOn, Param::delayOn, Param::reverbOn })
        if (on (p))
            return true;
    for (const auto* sw : { &filterSwitch, &satSwitch, &ottSwitch, &duckSwitch, &delaySwitch, &reverbSwitch })
        if (sw->amount.getCurrentValue() > 0.0f || sw->amount.isSmoothing())
            return true;
    return false;
}

void FxChain::process (float* const* io, int numChannels, int numSamples, const SidechainKey& key) noexcept
{
    numChannels = std::min (numChannels, 2);
    for (int pos = 0; pos < numSamples; pos += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - pos);
        float* chans[2] = { io[0] + pos, numChannels > 1 ? io[1] + pos : nullptr };
        const SidechainKey k { key.kickTriggers != nullptr ? key.kickTriggers + pos : nullptr,
                               key.stemLeft != nullptr ? key.stemLeft + pos : nullptr,
                               key.stemRight != nullptr ? key.stemRight + pos : nullptr };
        filter (chans, numChannels, n);
        saturation (chans, numChannels, n);
        ott (chans, numChannels, n);
        ducker (chans, numChannels, n, k);
        delay (chans, numChannels, n);
        reverb (chans, numChannels, n);
    }
}

//==============================================================================
void FxChain::filter (float* const* io, int numChannels, int n) noexcept
{
    bool started = false;
    if (! filterSwitch.update (on (Param::filterOn), started))
        return;
    if (started)
    {
        ic1.fill (0.0f);
        ic2.fill (0.0f);
        cutoff.setCurrentAndTargetValue (raw (Param::filterCutoff));
        resonance.setCurrentAndTargetValue (raw (Param::filterResonance));
    }
    cutoff.setTargetValue (raw (Param::filterCutoff));
    resonance.setTargetValue (raw (Param::filterResonance));
    const int type = juce::jlimit (0, 2, (int) std::lround (raw (Param::filterType)));
    for (int t = 0; t < 3; ++t)
        typeWeights[(size_t) t].setTargetValue (t == type ? 1.0f : 0.0f);

    const float nyquistLimit = (float) sampleRate * 0.49f;
    float g = 0, k = 0, a1 = 0, a2 = 0, a3 = 0;
    auto coefficients = [&] {
        g = std::tan (juce::MathConstants<float>::pi * std::min (cutoff.getCurrentValue(), nyquistLimit) / (float) sampleRate);
        k = 1.0f / resonance.getCurrentValue();
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    };
    coefficients();
    for (int i = 0; i < n; ++i)
    {
        if (cutoff.isSmoothing() || resonance.isSmoothing())
        {
            cutoff.getNextValue();
            resonance.getNextValue();
            coefficients();
        }
        const float wl = typeWeights[0].getNextValue(), wh = typeWeights[1].getNextValue(), wb = typeWeights[2].getNextValue();
        const float amount = filterSwitch.amount.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float x = io[ch][i];
            const float v3 = x - ic2[(size_t) ch];
            const float v1 = a1 * ic1[(size_t) ch] + a2 * v3;
            const float v2 = ic2[(size_t) ch] + a2 * ic1[(size_t) ch] + a3 * v3;
            ic1[(size_t) ch] = 2.0f * v1 - ic1[(size_t) ch];
            ic2[(size_t) ch] = 2.0f * v2 - ic2[(size_t) ch];
            const float high = x - k * v1 - v2;
            const float wet = wl * v2 + wh * high + wb * k * v1; // band-pass normalised to unity peak
            io[ch][i] = x + amount * (wet - x);
        }
    }
}

void FxChain::saturation (float* const* io, int numChannels, int n) noexcept
{
    bool started = false;
    if (! satSwitch.update (on (Param::satOn), started))
        return;
    drive.setTargetValue (juce::Decibels::decibelsToGain (raw (Param::satDrive)));
    satMix.setTargetValue (raw (Param::satMix));
    for (int i = 0; i < n; ++i)
    {
        const float d = drive.getNextValue();
        const float amount = satSwitch.amount.getNextValue() * satMix.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float x = io[ch][i];
            const float wet = std::tanh (d * x) / d; // unity small-signal gain, peaks rounded off
            io[ch][i] = x + amount * (wet - x);
        }
    }
}

void FxChain::ott (float* const* io, int numChannels, int n) noexcept
{
    bool started = false;
    if (! ottSwitch.update (on (Param::ottOn), started))
        return;
    if (started)
    {
        crossLow.reset();
        crossHigh.reset();
        lowAllpass.reset();
        ottEnv.fill (0.0f);
        ottWarmup = (int) (0.02 * sampleRate);
    }
    ottDepth.setTargetValue (raw (Param::ottDepth));
    ottTime.setTargetValue (raw (Param::ottTime));
    ottIn.setTargetValue (juce::Decibels::decibelsToGain (raw (Param::ottInGain)));
    ottOut.setTargetValue (juce::Decibels::decibelsToGain (raw (Param::ottOutGain)));

    std::array<float, 3> att {}, rel {};
    auto times = [&] {
        for (size_t b = 0; b < 3; ++b)
        {
            att[b] = coefficient (sampleRate, ottAttackMs[b] * ottTime.getCurrentValue());
            rel[b] = coefficient (sampleRate, ottReleaseMs[b] * ottTime.getCurrentValue());
        }
    };
    times();
    for (int i = 0; i < n; ++i)
    {
        if (ottTime.isSmoothing())
        {
            ottTime.getNextValue();
            times();
        }
        const float inGain = ottIn.getNextValue(), outGain = ottOut.getNextValue();
        const float depth = ottDepth.getNextValue();
        // switched on: the crossovers and detectors settle for 20 ms (output still dry) before the
        // crossfade starts, so their start-up transient never reaches the output
        const float amount = ottWarmup > 0 ? (--ottWarmup, 0.0f) : ottSwitch.amount.getNextValue();

        std::array<std::array<float, 3>, 2> bands {};
        std::array<float, 3> peak {};
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float low = 0, rest = 0, mid = 0, high = 0, apLow = 0, apHigh = 0;
            // the input gain is applied after the split: the same signal, but a gain ramp's corners
            // stay out of the near-silent high band that upward compression lifts
            crossLow.processSample (ch, io[ch][i], low, rest);
            crossHigh.processSample (ch, rest, mid, high);
            lowAllpass.processSample (ch, low, apLow, apHigh); // low band phase-matched to mid + high
            bands[(size_t) ch] = { (apLow + apHigh) * inGain, mid * inGain, high * inGain };
            for (size_t b = 0; b < 3; ++b)
                peak[b] = std::max (peak[b], std::abs (bands[(size_t) ch][b]));
        }
        std::array<float, 3> gain {};
        for (size_t b = 0; b < 3; ++b)
        {
            auto& env = ottEnv[b];
            const float c = peak[b] > env ? att[b] : rel[b];
            env = c * env + (1.0f - c) * peak[b];
            const float envDb = juce::Decibels::gainToDecibels (env, -120.0f);
            float gainDb = 0.0f;
            if (envDb > ottDownThresholdDb)
                gainDb = -(envDb - ottDownThresholdDb) * (1.0f - 1.0f / ottDownRatio);
            else if (envDb < ottUpThresholdDb)
                gainDb = std::min (ottMaxUpDb, (ottUpThresholdDb - envDb) * (1.0f - 1.0f / ottUpRatio))
                       * juce::jlimit (0.0f, 1.0f, (envDb - ottFloorDb) / ottFloorRampDb);
            gain[b] = juce::Decibels::decibelsToGain (gainDb * depth);
        }
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto& bnd = bands[(size_t) ch];
            const float wet = (bnd[0] * gain[0] + bnd[1] * gain[1] + bnd[2] * gain[2]) * outGain;
            io[ch][i] += amount * (wet - io[ch][i]);
        }
    }
}

void FxChain::ducker (float* const* io, int numChannels, int n, const SidechainKey& key) noexcept
{
    bool started = false;
    if (! duckSwitch.update (on (Param::duckOn), started))
        return;
    if (started)
    {
        duckEnv = 0.0f;
        duckGain = 1.0f;
    }
    duckDepth.setTargetValue (raw (Param::duckDepth));
    duckRelease.setTargetValue (raw (Param::duckRelease));
    const bool fromStem = (int) std::lround (raw (Param::duckSource)) == (int) DuckSource::drumsStem;
    const float attack = coefficient (sampleRate, 1.0f); // the gain falls over ~1 ms: no click
    float release = coefficient (sampleRate, duckRelease.getCurrentValue());
    for (int i = 0; i < n; ++i)
    {
        if (duckRelease.isSmoothing())
            release = coefficient (sampleRate, duckRelease.getNextValue());
        float k = 0.0f;
        if (fromStem && key.stemLeft != nullptr)
            k = 4.0f * std::max (std::abs (key.stemLeft[i]), key.stemRight != nullptr ? std::abs (key.stemRight[i]) : 0.0f); // -12 dBFS = full duck
        else if (! fromStem && key.kickTriggers != nullptr)
            k = key.kickTriggers[i];
        duckEnv = std::max (std::min (k, 1.0f), duckEnv * release);
        const float target = 1.0f - duckDepth.getNextValue() * duckEnv;
        duckGain = target < duckGain ? attack * duckGain + (1.0f - attack) * target : target; // release follows the envelope
        const float g = 1.0f + duckSwitch.amount.getNextValue() * (duckGain - 1.0f);
        for (int ch = 0; ch < numChannels; ++ch)
            io[ch][i] *= g;
    }
}

void FxChain::delay (float* const* io, int numChannels, int n) noexcept
{
    bool started = false;
    if (! delaySwitch.update (on (Param::delayOn), started))
        return;
    const int size = delayLines.getNumSamples();
    const int division = juce::jlimit (0, (int) delayDivisionBeats.size() - 1, (int) std::lround (raw (Param::delayDivision)));
    const int target = juce::jlimit (1, size - 1, (int) std::lround (delayDivisionBeats[(size_t) division] * 60.0 / tempo.load (std::memory_order_relaxed) * sampleRate));
    if (started)
    {
        delayLines.clear();
        delayNow = delayNext = target;
        tapFade = 1.0f;
        feedback.setCurrentAndTargetValue (raw (Param::delayFeedback));
        delayMix.setCurrentAndTargetValue (raw (Param::delayMix));
    }
    feedback.setTargetValue (raw (Param::delayFeedback));
    delayMix.setTargetValue (raw (Param::delayMix));
    const float fadeStep = 1.0f / (float) (sampleRate * tapFadeMs * 0.001);

    float* lineL = delayLines.getWritePointer (0);
    float* lineR = delayLines.getWritePointer (1);
    auto read = [size] (const float* line, int pos, int d) { int r = pos - d; if (r < 0) r += size; return line[r]; };
    for (int i = 0; i < n; ++i)
    {
        if (tapFade >= 1.0f && target != delayNow)
        {
            delayNext = target; // fade from the old tap to the new one
            tapFade = 0.0f;
        }
        float outL = read (lineL, writePos, delayNow), outR = read (lineR, writePos, delayNow);
        if (tapFade < 1.0f)
        {
            const float f = tapFade;
            outL += f * (read (lineL, writePos, delayNext) - outL);
            outR += f * (read (lineR, writePos, delayNext) - outR);
            tapFade += fadeStep;
            if (tapFade >= 1.0f)
                delayNow = delayNext;
        }
        const float xL = io[0][i], xR = numChannels > 1 ? io[1][i] : xL;
        const float fb = feedback.getNextValue();
        const float amount = delaySwitch.amount.getNextValue();
        // ping-pong: the input enters left, echoes alternate. The send fades with the switch too,
        // so the first echo starts as smoothly as the switch (not with the input's hard onset).
        lineL[writePos] = amount * 0.5f * (xL + xR) + fb * outR;
        lineR[writePos] = fb * outL;
        if (++writePos >= size)
            writePos = 0;
        const float wet = amount * delayMix.getNextValue();
        io[0][i] = xL + wet * (numChannels > 1 ? outL : 0.5f * (outL + outR));
        if (numChannels > 1)
            io[1][i] = xR + wet * outR;
    }
}

void FxChain::reverb (float* const* io, int numChannels, int n) noexcept
{
    bool started = false;
    if (! reverbSwitch.update (on (Param::reverbOn), started))
        return;
    juce::Reverb::Parameters p;
    p.roomSize = raw (Param::reverbSize);
    p.damping = raw (Param::reverbDamping);
    p.width = raw (Param::reverbWidth);
    p.wetLevel = 1.0f / 3.0f; // JUCE scales wet by 3: unity
    p.dryLevel = 0.0f;
    p.freezeMode = 0.0f;
    if (started)
    {
        reverbEngine.reset();
        reverbMix.setCurrentAndTargetValue (raw (Param::reverbMix));
    }
    if (started || p.roomSize != reverbParams.roomSize || p.damping != reverbParams.damping || p.width != reverbParams.width)
    {
        reverbParams = p;
        reverbEngine.setParameters (p); // JUCE smooths damping, size and the wet gains
    }
    reverbMix.setTargetValue (raw (Param::reverbMix));

    // the send fades with the switch (the tail of a hard onset would outlast the output fade)
    float* l = scratch.getWritePointer (0);
    float* r = scratch.getWritePointer (1);
    auto& fade = reverbSwitch.amount;
    float* fadeOut = scratch.getWritePointer (2);
    for (int i = 0; i < n; ++i)
    {
        const float a = fade.getNextValue();
        fadeOut[i] = a;
        l[i] = a * io[0][i];
        r[i] = a * (numChannels > 1 ? io[1][i] : io[0][i]);
    }
    reverbEngine.processStereo (l, r, n);
    for (int i = 0; i < n; ++i)
    {
        const float wet = fadeOut[i] * reverbMix.getNextValue(); // fades the tail out with the switch too
        if (numChannels > 1)
        {
            io[0][i] += wet * l[i];
            io[1][i] += wet * r[i];
        }
        else
            io[0][i] += wet * 0.5f * (l[i] + r[i]);
    }
}

} // namespace jaudios::fx
