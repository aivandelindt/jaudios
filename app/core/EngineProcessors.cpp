#include "EngineProcessors.h"

#include <algorithm>
#include <cmath>

namespace jaudios
{

//==============================================================================
SourcePlayerProcessor::SourcePlayerProcessor()
    : EngineProcessorBase (BusesProperties().withOutput ("Out", juce::AudioChannelSet::discreteChannels (numOutputChannels), true))
{
    owned.reserve (16);
}

bool SourcePlayerProcessor::setSource (std::shared_ptr<const Buffer> buffer)
{
    collectGarbage();
    const auto* raw = buffer.get();
    if (! commands.push ({ Command::Type::setSource, raw, 0 }))
        return false;
    return keep (std::move (buffer));
}

bool SourcePlayerProcessor::setStems (std::shared_ptr<const Buffer> buffer)
{
    jassert (buffer == nullptr || buffer->getNumChannels() == 2 * numStemLayers);
    collectGarbage();
    const auto* raw = buffer.get();
    if (! commands.push ({ Command::Type::setStems, raw, 0 }))
        return false;
    return keep (std::move (buffer));
}

bool SourcePlayerProcessor::keep (std::shared_ptr<const Buffer> buffer)
{
    if (buffer != nullptr)
        owned.push_back (std::move (buffer));
    return true;
}

void SourcePlayerProcessor::retire (const Buffer*& slot) noexcept
{
    if (slot != nullptr)
        retired.push (slot); // queue sized so this cannot overflow in practice
    slot = nullptr;
}

bool SourcePlayerProcessor::play() { return commands.push ({ Command::Type::play, nullptr, 0 }); }
bool SourcePlayerProcessor::stop() { return commands.push ({ Command::Type::stop, nullptr, 0 }); }
bool SourcePlayerProcessor::seek (juce::int64 pos) { return commands.push ({ Command::Type::seek, nullptr, pos }); }

void SourcePlayerProcessor::collectGarbage()
{
    const Buffer* old = nullptr;
    while (retired.pop (old))
    {
        auto it = std::find_if (owned.begin(), owned.end(), [old] (const auto& p) { return p.get() == old; });
        if (it != owned.end())
            owned.erase (it);
    }
}

void SourcePlayerProcessor::handleCommands() noexcept
{
    Command cmd;
    while (commands.pop (cmd))
    {
        switch (cmd.type)
        {
            case Command::Type::setSource:
                retire (current);
                retire (stems);
                current = cmd.buffer;
                readPosition = 0;
                playing = false;
                break;
            case Command::Type::setStems:
                retire (stems);
                stems = cmd.buffer;
                break;
            case Command::Type::play:  playing = current != nullptr; break;
            case Command::Type::stop:  playing = false; break;
            case Command::Type::seek:  readPosition = std::max<juce::int64> (0, cmd.position); break;
        }
    }
}

void SourcePlayerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    handleCommands();
    buffer.clear();

    const int numSamples = buffer.getNumSamples();

    if (playing && current != nullptr)
    {
        const auto length = (juce::int64) current->getNumSamples();
        const int n = (int) std::clamp<juce::int64> (length - readPosition, 0, numSamples);
        const int srcChannels = current->getNumChannels();

        for (int ch = 0; ch < 2 && ch < buffer.getNumChannels() && n > 0; ++ch)
            buffer.copyFrom (ch, 0, *current, std::min (ch, srcChannels - 1), (int) readPosition, n);

        if (stems != nullptr)
        {
            const int m = (int) std::clamp<juce::int64> ((juce::int64) stems->getNumSamples() - readPosition, 0, n);
            for (int ch = 0; ch < 2 * numStemLayers && 2 + ch < buffer.getNumChannels() && m > 0; ++ch)
                buffer.copyFrom (2 + ch, 0, *stems, ch, (int) readPosition, m);
        }

        readPosition += n;
        if (readPosition >= length)
            playing = false;
    }

    positionForUi.store (readPosition, std::memory_order_relaxed);
    playingForUi.store (playing, std::memory_order_relaxed);
}

//==============================================================================
MidiClip makeClip (const juce::MidiMessageSequence& seq, double transportRate)
{
    MidiClip clip;
    clip.reserve ((size_t) seq.getNumEvents());
    for (const auto* e : seq)
        clip.push_back ({ (juce::int64) std::llround (e->message.getTimeStamp() * transportRate), e->message });
    std::stable_sort (clip.begin(), clip.end(), [] (const ClipEvent& a, const ClipEvent& b) { return a.position < b.position; });
    return clip;
}

ClipPlayerProcessor::ClipPlayerProcessor (juce::String n) : EngineProcessorBase (stereoOutOnly()), name (std::move (n))
{
    ownedClips.reserve (8);
    ownedInstruments.reserve (8);
}

bool ClipPlayerProcessor::setClip (std::shared_ptr<const MidiClip> c)
{
    collectGarbage();
    if (! commands.push ({ Command::Type::setClip, c.get(), nullptr, 0 }))
        return false;
    if (c != nullptr)
        ownedClips.push_back (std::move (c));
    return true;
}

bool ClipPlayerProcessor::setInstrument (std::shared_ptr<synth::Instrument> i)
{
    collectGarbage();
    if (! commands.push ({ Command::Type::setInstrument, nullptr, i.get(), 0 }))
        return false;
    if (i != nullptr)
        ownedInstruments.push_back (std::move (i));
    return true;
}

bool ClipPlayerProcessor::play() { return commands.push ({ Command::Type::play, nullptr, nullptr, 0 }); }
bool ClipPlayerProcessor::stop() { return commands.push ({ Command::Type::stop, nullptr, nullptr, 0 }); }
bool ClipPlayerProcessor::seek (juce::int64 pos) { return commands.push ({ Command::Type::seek, nullptr, nullptr, pos }); }
bool ClipPlayerProcessor::setLength (juce::int64 len) { return commands.push ({ Command::Type::setLength, nullptr, nullptr, len }); }

void ClipPlayerProcessor::collectGarbage()
{
    Retired r;
    while (retired.pop (r))
    {
        if (r.clip != nullptr)
            if (auto it = std::find_if (ownedClips.begin(), ownedClips.end(), [&] (const auto& p) { return p.get() == r.clip; }); it != ownedClips.end())
                ownedClips.erase (it);
        if (r.instrument != nullptr)
            if (auto it = std::find_if (ownedInstruments.begin(), ownedInstruments.end(), [&] (const auto& p) { return p.get() == r.instrument; }); it != ownedInstruments.end())
                ownedInstruments.erase (it);
    }
}

void ClipPlayerProcessor::prepareToPlay (double sampleRate, int maxBlockSize)
{
    // the audio callback is stopped while the graph prepares: safe to (re)prepare every instrument
    for (auto& i : ownedInstruments)
        i->prepare (sampleRate, maxBlockSize);
    blockMidi.ensureSize (4096);
}

void ClipPlayerProcessor::relocate() noexcept
{
    cursor = 0;
    if (clip == nullptr)
        return;
    const auto it = std::lower_bound (clip->begin(), clip->end(), readPosition,
                                      [] (const ClipEvent& e, juce::int64 p) { return e.position < p; });
    cursor = (size_t) (it - clip->begin());
}

void ClipPlayerProcessor::handleCommands() noexcept
{
    Command cmd;
    while (commands.pop (cmd))
    {
        switch (cmd.type)
        {
            case Command::Type::setClip:
                if (instrument != nullptr)
                    instrument->allNotesOff();
                if (clip != nullptr)
                    retired.push ({ clip, nullptr });
                clip = cmd.clip;
                relocate();
                break;
            case Command::Type::setInstrument:
                if (instrument != nullptr)
                    retired.push ({ nullptr, instrument });
                instrument = cmd.instrument;
                break;
            case Command::Type::play:
                playing = true;
                break;
            case Command::Type::stop:
                playing = false;
                if (instrument != nullptr)
                    instrument->allNotesOff();
                break;
            case Command::Type::seek:
                readPosition = std::max<juce::int64> (0, cmd.position);
                relocate();
                if (instrument != nullptr)
                    instrument->allNotesOff();
                break;
            case Command::Type::setLength: // a new source: like the SourcePlayer, back to the start, stopped
                length = cmd.position;
                readPosition = 0;
                relocate();
                playing = false;
                if (instrument != nullptr)
                    instrument->allNotesOff();
                break;
        }
    }
}

void ClipPlayerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    handleCommands();
    // the SourcePlayer's arithmetic: play up to the source's end, then stop on the same sample
    const int n = playing && length >= 0 ? (int) std::clamp<juce::int64> (length - readPosition, 0, buffer.getNumSamples())
                                         : buffer.getNumSamples();
    blockMidi.clear();
    if (playing && clip != nullptr)
    {
        const juce::int64 end = readPosition + n;
        while (cursor < clip->size() && (*clip)[cursor].position < end)
        {
            const auto& e = (*clip)[cursor++];
            blockMidi.addEvent (e.message, (int) std::max<juce::int64> (0, e.position - readPosition)); // fits the reserved space
        }
    }
    if (instrument != nullptr)
        instrument->process (buffer, blockMidi); // also renders release tails while stopped
    else
        buffer.clear();
    if (playing)
    {
        readPosition += n;
        if (length >= 0 && readPosition >= length)
        {
            playing = false;
            if (instrument != nullptr)
                instrument->allNotesOff();
        }
    }
}

//==============================================================================
ChannelStripProcessor::ChannelStripProcessor (juce::String n)
    : EngineProcessorBase (stereoInOut()), name (std::move (n))
{
}

void ChannelStripProcessor::setGainDecibels (float dB) noexcept
{
    gainLinear.store (juce::Decibels::decibelsToGain (dB, -100.0f), std::memory_order_relaxed);
}

void ChannelStripProcessor::prepareToPlay (double sampleRate, int)
{
    smoother.reset (sampleRate, 0.02);
    smoother.setCurrentAndTargetValue (muted.load() ? 0.0f : gainLinear.load());
}

void ChannelStripProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    smoother.setTargetValue (muted.load (std::memory_order_relaxed) ? 0.0f
                                                                     : gainLinear.load (std::memory_order_relaxed));

    if (! smoother.isSmoothing())
    {
        buffer.applyGain (smoother.getTargetValue());
        return;
    }

    const int numChannels = buffer.getNumChannels();
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float g = smoother.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }
}

} // namespace jaudios
