#pragma once

#include "Timeline.h"

#include "app/core/SongController.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jaudios::gui
{

/** Min/max overview of a buffer, computed off the message thread (the cached thumbnail the
    source lane draws from: a repaint never reads the audio). */
struct WaveformPeaks
{
    std::vector<float> minima, maxima; // one pair per bucket, channels mixed
    double durationSec = 0.0;

    static WaveformPeaks compute (const juce::AudioBuffer<float>& buffer, double sampleRate, int numBuckets = 16384);
};

/** A lane on the shared timeline: bar / beat lines behind, content on top. */
class Lane : public juce::Component, private juce::ChangeListener
{
public:
    Lane (TimelineModel&, juce::Colour accent);
    ~Lane() override;

    void setSong (SongController*);
    void paint (juce::Graphics&) override;

protected:
    virtual void paintContent (juce::Graphics&) = 0;
    void paintGrid (juce::Graphics&, bool barNumbers) const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    TimelineModel& timeline;
    SongController* song = nullptr;
    juce::Colour accent;
};

/** The source: waveform (from cached peaks) with the bar grid and bar numbers; click to seek. */
class SourceLane final : public Lane
{
public:
    explicit SourceLane (TimelineModel&);
    void setPeaks (WaveformPeaks);
    std::function<void (double)> onSeek;
    void mouseDown (const juce::MouseEvent& e) override { seekTo ((float) e.x); }
    void mouseDrag (const juce::MouseEvent& e) override { seekTo ((float) e.x); }

private:
    void seekTo (float x);
    void paintContent (juce::Graphics&) override;
    WaveformPeaks peaks;
};

/** One block per chord slot: chord name and Roman numeral; a dot marks a replaced chord. Click
    a block for its alternatives. */
class ChordLane final : public Lane
{
public:
    explicit ChordLane (TimelineModel&);
    /** The slot under x (lane coordinates), or -1. */
    int slotAt (float x) const;
    juce::Rectangle<int> slotBounds (int slot) const;
    std::function<void (int slot)> onChordClicked;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void paintContent (juce::Graphics&) override;
};

/** The bassline as a piano roll over its pitch range (sidechain-hinted notes outlined). */
class BassLane final : public Lane
{
public:
    explicit BassLane (TimelineModel&);

private:
    void paintContent (juce::Graphics&) override;
};

/** The groove: one row per drum element, velocity as brightness. */
class DrumLane final : public Lane
{
public:
    explicit DrumLane (TimelineModel&);

private:
    void paintContent (juce::Graphics&) override;
};

/** The playhead over all lanes (mouse-transparent). setPosition repaints only the thin strips
    the line leaves and enters, so a 60 Hz update costs two narrow repaints. */
class PlayheadOverlay final : public juce::Component
{
public:
    explicit PlayheadOverlay (TimelineModel&);
    void setPosition (double seconds);
    double position() const noexcept { return seconds; }
    void paint (juce::Graphics&) override;

private:
    TimelineModel& timeline;
    double seconds = 0.0;
    int lastX = -1;
};

} // namespace jaudios::gui
