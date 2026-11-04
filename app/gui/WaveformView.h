#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace jaudios
{

/** Min/max overview of a buffer, computed off the message thread. */
struct WaveformPeaks
{
    std::vector<float> minima, maxima; // one pair per bucket, channels mixed
    double durationSec = 0.0;

    static WaveformPeaks compute (const juce::AudioBuffer<float>& buffer, double sampleRate, int numBuckets = 4096);
};

/** Source waveform with playhead and bar grid (ui-shell will restyle this). */
class WaveformView final : public juce::Component
{
public:
    void setPeaks (WaveformPeaks newPeaks);
    void setBarGrid (std::vector<double> downbeatTimesSec, std::vector<double> beatTimesSec);
    void setPlayheadSec (double t);

    std::function<void (double)> onSeek;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    float timeToX (double t) const;

    WaveformPeaks peaks;
    std::vector<double> downbeats, beats;
    double playhead = 0.0;
};

} // namespace jaudios
