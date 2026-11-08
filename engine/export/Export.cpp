#include "Export.h"

#include <algorithm>
#include <cmath>

namespace jaudios::exporting
{

BarGrid BarGrid::fromAnalysis (const std::vector<double>& beats, const std::vector<double>& downbeats, double bpm, int beatsPerBar)
{
    BarGrid g;
    g.tempo = bpm > 0.0 ? bpm : 120.0;
    g.perBar = std::max (1, beatsPerBar);
    const double spb = 60.0 / g.tempo;
    if (beats.empty())
    {
        g.times = { 0.0 };
        g.firstBeat = 0.0;
        return g;
    }
    g.times = beats;
    // the first downbeat's index among the beats (beat number 0 before prepending bars)
    size_t p = 0;
    if (! downbeats.empty())
    {
        const auto it = std::min_element (beats.begin(), beats.end(),
                                          [&] (double a, double b) { return std::abs (a - downbeats.front()) < std::abs (b - downbeats.front()); });
        p = (size_t) (it - beats.begin());
    }
    // prepend whole bars until beat 0 is at (or within 5 ms after) the start of the source
    auto localToSeconds = [&] (double local) {
        const double idx = local + (double) p;
        return idx < 0.0 ? beats.front() + idx * spb : beats[std::min ((size_t) idx, beats.size() - 1)];
    };
    int bars = 0;
    while (localToSeconds (-(double) (bars * g.perBar)) > 0.005)
        ++bars;
    g.firstBeat = -(double) p + (double) (bars * g.perBar);
    return g;
}

double BarGrid::toSeconds (double beat) const noexcept
{
    const double spb = 60.0 / tempo;
    const double idx = beat - firstBeat;
    if (idx <= 0.0)
        return times.front() + idx * spb;
    const double last = (double) (times.size() - 1);
    if (idx >= last)
        return times.back() + (idx - last) * spb;
    const auto i = (size_t) idx;
    const double f = idx - (double) i;
    return times[i] + f * (times[i + 1] - times[i]);
}

double BarGrid::toBeats (double seconds) const noexcept
{
    const double spb = 60.0 / tempo;
    if (seconds <= times.front())
        return firstBeat + (seconds - times.front()) / spb;
    if (seconds >= times.back())
        return firstBeat + (double) (times.size() - 1) + (seconds - times.back()) / spb;
    const auto it = std::upper_bound (times.begin(), times.end(), seconds);
    const auto i = (size_t) (it - times.begin()) - 1;
    const double span = times[i + 1] - times[i];
    return firstBeat + (double) i + (span > 0.0 ? (seconds - times[i]) / span : 0.0);
}

int keySignatureSharps (theory::Key key) noexcept
{
    // the relative major's place on the circle of fifths, flats for F..Db
    static constexpr int sharps[12] = { 0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5 };
    const int major = key.mode == theory::Mode::minor ? (key.tonic + 3) % 12 : key.tonic % 12;
    return sharps[major];
}

juce::MidiFile makeMidiFile (const juce::String& songName, const std::vector<MidiPart>& parts, const BarGrid& grid,
                             std::optional<theory::Key> key, int ppq)
{
    auto ticksOf = [&] (double seconds) { return std::max (0.0, std::round (grid.toBeats (seconds) * ppq)); };

    double lastBeat = (double) grid.beatsPerBar();
    for (const auto& part : parts)
        for (const auto* e : part.events)
            lastBeat = std::max (lastBeat, grid.toBeats (e->message.getTimeStamp()));

    juce::MidiMessageSequence conductor;
    conductor.addEvent (juce::MidiMessage::textMetaEvent (3, songName), 0.0);
    conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (grid.beatsPerBar(), 4), 0.0);
    if (key)
        conductor.addEvent (juce::MidiMessage::keySignatureMetaEvent (keySignatureSharps (*key), key->mode == theory::Mode::minor), 0.0);
    // the tempo map: one event per beat whose length differs from the last one written by > 0.1 %
    int lastMicros = 0;
    for (int b = 0; b <= (int) std::ceil (lastBeat); ++b)
    {
        const int micros = (int) std::lround (grid.beatLength (b) * 1.0e6);
        if (lastMicros == 0 || std::abs (micros - lastMicros) > lastMicros / 1000)
        {
            conductor.addEvent (juce::MidiMessage::tempoMetaEvent (micros), (double) b * ppq);
            lastMicros = micros;
        }
    }

    juce::MidiFile file;
    file.setTicksPerQuarterNote (ppq);
    file.addTrack (conductor);
    for (const auto& part : parts)
    {
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::textMetaEvent (3, part.name), 0.0);
        for (const auto* e : part.events)
            track.addEvent (e->message, ticksOf (e->message.getTimeStamp()) - e->message.getTimeStamp());
        track.sort();
        track.updateMatchedPairs();
        file.addTrack (track);
    }
    return file;
}

bool writeMidiFile (const juce::File& dest, const juce::MidiFile& file)
{
    dest.getParentDirectory().createDirectory();
    dest.deleteFile();
    juce::FileOutputStream out (dest);
    return out.openedOk() && file.writeTo (out, 1);
}

juce::AudioBuffer<float> alignToGrid (const juce::AudioBuffer<float>& rendered, double sampleRate, const BarGrid& grid)
{
    const int shift = (int) std::lround (-grid.originSeconds() * sampleRate); // > 0: silence before the source
    const int length = std::max (0, rendered.getNumSamples() + shift);
    juce::AudioBuffer<float> out (rendered.getNumChannels(), length);
    out.clear();
    for (int ch = 0; ch < rendered.getNumChannels(); ++ch)
    {
        const int from = std::max (0, -shift);
        const int to = std::max (0, shift);
        if (rendered.getNumSamples() > from)
            out.copyFrom (ch, to, rendered, ch, from, rendered.getNumSamples() - from);
    }
    return out;
}

bool writeWav (const juce::File& dest, const juce::AudioBuffer<float>& b, double sampleRate, WavFormat format)
{
    dest.getParentDirectory().createDirectory();
    dest.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (dest);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels (b.getNumChannels())
                                                   .withBitsPerSample (format == WavFormat::float32 ? 32 : 24));
    if (writer == nullptr)
        return false;
    if (format == WavFormat::int24 && b.getMagnitude (0, b.getNumSamples()) > 1.0f)
    {
        juce::AudioBuffer<float> clipped (b); // integer PCM: clip, as a DAW bounce would
        for (int ch = 0; ch < clipped.getNumChannels(); ++ch)
            juce::FloatVectorOperations::clip (clipped.getWritePointer (ch), clipped.getReadPointer (ch), -1.0f, 1.0f, clipped.getNumSamples());
        return writer->writeFromAudioSampleBuffer (clipped, 0, clipped.getNumSamples());
    }
    return writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

juce::String safeFileName (const juce::String& name)
{
    // only what file systems reject (createLegalFileName also drops commas and more)
    return name.removeCharacters ("/\\:*?\"<>|").replaceCharacters ("\t\r\n", "   ").trim();
}

} // namespace jaudios::exporting
