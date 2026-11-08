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

/** Accent editing shared by the bass and drum lanes: a click cycles accent -> ghost -> off -> back,
    a vertical drag sets the velocity (up = louder), all through the SongController (undoable). */
class EditableLane : public Lane
{
public:
    using Lane::Lane;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    std::function<void (const juce::String&)> onStatus;

protected:
    struct Target { bool valid = false; SongController::DrumHit hit; int bassStep = 0; bool isBass = false; };
    virtual Target targetAt (juce::Point<float>) const = 0;
    int velocityOf (const Target&) const;
    juce::String describe (const Target&) const;
    void paintEditMark (juce::Graphics&, juce::Rectangle<float>) const;

private:
    Target pressed;
    int startVelocity = 0;
    bool dragging = false;
};

/** The bassline as a piano roll over its pitch range (sidechain-hinted notes outlined, switched-off
    notes hollow). Click / drag a note to edit its accent. */
class BassLane final : public EditableLane
{
public:
    explicit BassLane (TimelineModel&);
    /** The note's rectangle (lane coordinates) for a note of the generated line. */
    juce::Rectangle<float> noteBounds (const generators::BassNote&) const;
    Target targetAt (juce::Point<float>) const override;

private:
    void paintContent (juce::Graphics&) override;
    std::pair<int, int> pitchRange() const;
};

/** The groove: one row per drum element, velocity as brightness, switched-off hits hollow. Click a
    hit to cycle its accent (an empty cell adds a hit), drag it up or down for its velocity. */
class DrumLane final : public EditableLane
{
public:
    explicit DrumLane (TimelineModel&);
    /** The hit place under a point: element by row, bar + nearest 1/16 step by time. */
    Target targetAt (juce::Point<float>) const override;
    juce::Rectangle<float> hitBounds (const generators::DrumNote&) const;
    float rowHeight() const noexcept { return (float) (getHeight() - 4) / (float) generators::numDrumElements; }

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
