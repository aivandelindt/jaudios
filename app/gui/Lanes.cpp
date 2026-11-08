#include "Lanes.h"

#include "Theme.h"

#include "engine/generators/DrumGenerator.h"

namespace jaudios::gui
{

WaveformPeaks WaveformPeaks::compute (const juce::AudioBuffer<float>& buffer, double sampleRate, int numBuckets)
{
    WaveformPeaks p;
    const int n = buffer.getNumSamples();
    p.durationSec = n / sampleRate;
    numBuckets = std::max (1, std::min (numBuckets, n));
    p.minima.assign ((size_t) numBuckets, 0.0f);
    p.maxima.assign ((size_t) numBuckets, 0.0f);
    for (int b = 0; b < numBuckets && n > 0; ++b)
    {
        const int start = (int) ((juce::int64) b * n / numBuckets);
        const int end = std::max (start + 1, (int) ((juce::int64) (b + 1) * n / numBuckets));
        float lo = 0.0f, hi = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (buffer.getReadPointer (ch, start), end - start);
            lo = std::min (lo, range.getStart());
            hi = std::max (hi, range.getEnd());
        }
        p.minima[(size_t) b] = lo;
        p.maxima[(size_t) b] = hi;
    }
    return p;
}

//==============================================================================
Lane::Lane (TimelineModel& t, juce::Colour c) : timeline (t), accent (c)
{
    timeline.addChangeListener (this);
    setOpaque (true);
}

Lane::~Lane() { timeline.removeChangeListener (this); }

void Lane::setSong (SongController* s)
{
    song = s;
    repaint();
}

void Lane::paint (juce::Graphics& g)
{
    g.fillAll (colours::lane);
    paintGrid (g, false);
    if (song != nullptr)
        paintContent (g);
    g.setColour (colours::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void Lane::paintGrid (juce::Graphics& g, bool barNumbers) const
{
    if (song == nullptr)
        return;
    const auto& rhythm = song->song().analysis.rhythm;
    const double t0 = timeline.start(), t1 = t0 + timeline.length();
    // beats only when they are at least 6 px apart
    if (rhythm.bpm > 0.0 && 60.0 / rhythm.bpm / timeline.secondsPerPixel() >= 6.0)
    {
        g.setColour (colours::beatLine);
        for (double b : rhythm.beats)
            if (b >= t0 && b <= t1)
                g.drawVerticalLine (juce::roundToInt (timeline.toX (b)), 0.0f, (float) getHeight());
    }
    g.setFont (juce::FontOptions (11.0f));
    for (size_t i = 0; i < rhythm.downbeats.size(); ++i)
    {
        const double d = rhythm.downbeats[i];
        if (d < t0 || d > t1)
            continue;
        const int x = juce::roundToInt (timeline.toX (d));
        g.setColour (colours::barLine);
        g.drawVerticalLine (x, 0.0f, (float) getHeight());
        if (barNumbers)
        {
            // the export grid's bar number, so the lane and an exported file agree
            const int bar = (int) std::lround (song->grid().toBeats (d) / song->grid().beatsPerBar()) + 1;
            g.setColour (colours::textDim);
            g.drawText (juce::String (bar), x + 3, 2, 40, 12, juce::Justification::left);
        }
    }
}

//==============================================================================
SourceLane::SourceLane (TimelineModel& t) : Lane (t, colours::source) {}

void SourceLane::setPeaks (WaveformPeaks p)
{
    peaks = std::move (p);
    repaint();
}

void SourceLane::paintContent (juce::Graphics& g)
{
    paintGrid (g, true);
    if (peaks.minima.empty() || peaks.durationSec <= 0.0)
        return;
    const float mid = (float) getHeight() * 0.5f + 6.0f, half = (float) (getHeight() - 16) * 0.5f;
    const double bucketsPerSecond = (double) peaks.minima.size() / peaks.durationSec;
    g.setColour (accent.withAlpha (0.85f));
    for (int x = 0; x < getWidth(); ++x)
    {
        const double a = timeline.toSeconds ((float) x), b = timeline.toSeconds ((float) x + 1.0f);
        const auto from = (size_t) juce::jlimit (0.0, (double) peaks.minima.size() - 1, a * bucketsPerSecond);
        const auto to = (size_t) juce::jlimit ((double) from + 1, (double) peaks.minima.size(), b * bucketsPerSecond);
        float lo = 0.0f, hi = 0.0f;
        for (size_t k = from; k < to; ++k)
        {
            lo = std::min (lo, peaks.minima[k]);
            hi = std::max (hi, peaks.maxima[k]);
        }
        g.drawVerticalLine (x, mid - hi * half, mid - lo * half + 1.0f);
    }
}

void SourceLane::seekTo (float x)
{
    if (onSeek)
        onSeek (juce::jlimit (0.0, timeline.duration(), timeline.toSeconds (x)));
}

//==============================================================================
ChordLane::ChordLane (TimelineModel& t) : Lane (t, colours::chords) {}

juce::Rectangle<int> ChordLane::slotBounds (int slot) const
{
    if (song == nullptr || slot < 0 || slot >= (int) song->currentProgression().slots.size())
        return {};
    const auto& s = song->currentProgression().slots[(size_t) slot];
    const int x0 = juce::roundToInt (timeline.toX (s.startSec)), x1 = juce::roundToInt (timeline.toX (s.endSec));
    return { x0 + 1, 4, std::max (1, x1 - x0 - 2), getHeight() - 8 };
}

int ChordLane::slotAt (float x) const
{
    if (song == nullptr)
        return -1;
    const double t = timeline.toSeconds (x);
    const auto& slots = song->currentProgression().slots;
    for (size_t i = 0; i < slots.size(); ++i)
        if (t >= slots[i].startSec && t < slots[i].endSec)
            return (int) i;
    return -1;
}

void ChordLane::paintContent (juce::Graphics& g)
{
    const auto& p = song->currentProgression();
    const auto key = song->song().source.key;
    const auto romans = p.romans (key);
    for (int i = 0; i < (int) p.chords.size(); ++i)
    {
        const auto r = slotBounds (i);
        if (r.getRight() < 0 || r.getX() > getWidth())
            continue;
        g.setColour (accent.withAlpha (0.22f));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (accent.withAlpha (0.8f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.0f);
        if (r.getWidth() >= 14)
        {
            // the name shrinks with the block (zoomed out) rather than turning into "..."
            const auto name = juce::String (p.chords[(size_t) i].name (key));
            const float size = juce::jlimit (9.0f, juce::jmin (18.0f, (float) r.getHeight() * 0.42f),
                                             (float) (r.getWidth() - 6) / (0.62f * (float) std::max (1, name.length())));
            g.setColour (colours::text);
            g.setFont (juce::FontOptions (size, juce::Font::bold));
            g.drawFittedText (name, r.reduced (3, 4).removeFromTop (r.getHeight() / 2 + 2), juce::Justification::centredLeft, 1, 0.7f);
            if (r.getWidth() >= 22)
            {
                g.setColour (colours::textDim);
                g.setFont (juce::FontOptions (juce::jmin (11.0f, size)));
                g.drawFittedText (romans[(size_t) i], r.reduced (3, 4).removeFromBottom (r.getHeight() / 2 - 4), juce::Justification::centredLeft, 1, 0.7f);
            }
        }
        if (song->isReplaced (i))
        {
            g.setColour (accent);
            g.fillEllipse ((float) r.getRight() - 9.0f, (float) r.getY() + 4.0f, 5.0f, 5.0f);
        }
    }
}

void ChordLane::mouseDown (const juce::MouseEvent& e)
{
    const int slot = slotAt ((float) e.x);
    if (slot >= 0 && onChordClicked)
        onChordClicked (slot);
}

//==============================================================================
BassLane::BassLane (TimelineModel& t) : Lane (t, colours::bass) {}

void BassLane::paintContent (juce::Graphics& g)
{
    const auto& notes = song->parts().bassNotes;
    if (notes.empty())
        return;
    int lo = 127, hi = 0;
    for (const auto& n : notes)
    {
        lo = std::min (lo, n.pitch);
        hi = std::max (hi, n.pitch);
    }
    lo -= 1;
    hi += 1;
    const float rowH = (float) (getHeight() - 6) / (float) (hi - lo + 1);
    for (const auto& n : notes)
    {
        const float x0 = timeline.toX (n.startSec), x1 = timeline.toX (n.endSec);
        if (x1 < 0.0f || x0 > (float) getWidth())
            continue;
        const juce::Rectangle<float> r (x0, 3.0f + (float) (hi - n.pitch) * rowH, std::max (2.0f, x1 - x0 - 1.0f), std::max (2.0f, rowH - 1.0f));
        g.setColour (accent.withAlpha (0.35f + 0.6f * (float) n.velocity / 127.0f));
        g.fillRect (r);
        if (n.sidechain)
        {
            g.setColour (colours::drums.withAlpha (0.7f));
            g.drawRect (r, 1.0f);
        }
    }
}

//==============================================================================
DrumLane::DrumLane (TimelineModel& t) : Lane (t, colours::drums) {}

void DrumLane::paintContent (juce::Graphics& g)
{
    const auto& notes = song->parts().drumNotes;
    const int rows = generators::numDrumElements;
    const float rowH = (float) (getHeight() - 4) / (float) rows;
    for (const auto& n : notes)
    {
        const float x = timeline.toX (n.startSec);
        if (x < -4.0f || x > (float) getWidth())
            continue;
        const float y = 2.0f + rowH * (float) (int) n.element;
        g.setColour (accent.withAlpha (0.25f + 0.75f * (float) n.velocity / 127.0f));
        g.fillRect (juce::Rectangle<float> (x, y + 1.0f, std::max (2.0f, (float) (0.05 / timeline.secondsPerPixel())), std::max (1.0f, rowH - 2.0f)));
    }
}

//==============================================================================
PlayheadOverlay::PlayheadOverlay (TimelineModel& t) : timeline (t)
{
    setInterceptsMouseClicks (false, false);
}

void PlayheadOverlay::setPosition (double s)
{
    seconds = s;
    const int x = juce::roundToInt (timeline.toX (s));
    if (x == lastX)
        return;
    if (lastX >= 0)
        repaint (lastX - 2, 0, 5, getHeight());
    repaint (x - 2, 0, 5, getHeight());
    lastX = x;
}

void PlayheadOverlay::paint (juce::Graphics& g)
{
    const float x = timeline.toX (seconds);
    if (x < 0.0f || x > (float) getWidth())
        return;
    g.setColour (colours::playhead.withAlpha (0.9f));
    g.drawLine (x, 0.0f, x, (float) getHeight(), 1.5f);
}

} // namespace jaudios::gui
