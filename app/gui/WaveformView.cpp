#include "WaveformView.h"

namespace jaudios
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

void WaveformView::setPeaks (WaveformPeaks newPeaks)
{
    peaks = std::move (newPeaks);
    downbeats.clear();
    beats.clear();
    playhead = 0.0;
    repaint();
}

void WaveformView::setBarGrid (std::vector<double> downbeatTimesSec, std::vector<double> beatTimesSec)
{
    downbeats = std::move (downbeatTimesSec);
    beats = std::move (beatTimesSec);
    repaint();
}

void WaveformView::setPlayheadSec (double t)
{
    if (std::abs (t - playhead) * getWidth() > peaks.durationSec * 0.5) // repaint only on pixel change
    {
        playhead = t;
        repaint();
    }
}

float WaveformView::timeToX (double t) const
{
    return peaks.durationSec > 0.0 ? (float) (t / peaks.durationSec * getWidth()) : 0.0f;
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff15171c));
    g.fillRoundedRectangle (bounds, 4.0f);

    if (peaks.maxima.empty())
    {
        g.setColour (juce::Colours::grey);
        g.drawText ("Drop an audio file here or use Open...", getLocalBounds(), juce::Justification::centred);
        return;
    }

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    for (double t : beats)
        g.drawVerticalLine ((int) timeToX (t), bounds.getY(), bounds.getBottom());

    g.setColour (juce::Colour (0xffe0a040).withAlpha (0.6f));
    for (double t : downbeats)
        g.drawVerticalLine ((int) timeToX (t), bounds.getY(), bounds.getBottom());

    const float mid = bounds.getCentreY();
    const float half = bounds.getHeight() * 0.45f;
    const int width = getWidth();
    const auto numBuckets = (int) peaks.maxima.size();

    g.setColour (juce::Colour (0xff5fb3c9));
    for (int x = 0; x < width; ++x)
    {
        const int b0 = (int) ((juce::int64) x * numBuckets / width);
        const int b1 = std::max (b0 + 1, (int) ((juce::int64) (x + 1) * numBuckets / width));
        float lo = 0.0f, hi = 0.0f;
        for (int b = b0; b < b1 && b < numBuckets; ++b)
        {
            lo = std::min (lo, peaks.minima[(size_t) b]);
            hi = std::max (hi, peaks.maxima[(size_t) b]);
        }
        g.drawVerticalLine (x, mid - hi * half, mid - lo * half + 1.0f);
    }

    g.setColour (juce::Colours::white);
    g.drawVerticalLine ((int) timeToX (playhead), bounds.getY(), bounds.getBottom());
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (onSeek && peaks.durationSec > 0.0 && getWidth() > 0)
        onSeek (juce::jlimit (0.0, peaks.durationSec, e.position.x / (double) getWidth() * peaks.durationSec));
}

} // namespace jaudios
