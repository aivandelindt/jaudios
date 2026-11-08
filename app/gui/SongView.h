#pragma once

#include "Lanes.h"
#include "Panels.h"

namespace jaudios::gui
{

/** The song editor (ui-shell layout). Top: the source waveform with its bar grid and, once
    separated, the stems' strips. Middle: the chord lane (click a chord for alternatives), the bass
    and drum lanes, each with its channel's mute / solo / gain / FX. Right: the export tiles.
    Pinch or Cmd+scroll zooms around the pointer, scroll pans; Space plays / stops, Cmd+Z / Cmd+Shift+Z
    undo / redo. The playhead updates at 60 Hz with two narrow repaints. */
class SongView final : public juce::Component, private juce::Timer
{
public:
    SongView();
    ~SongView() override;

    /** Shows `song` (null = empty view). `dragFolder` receives the export tiles' files. */
    void setSong (SongController*, WaveformPeaks, const juce::File& dragFolder);
    SongController* currentSong() const noexcept { return song; }

    TimelineModel& timeline() noexcept { return model; }
    ChordLane& chordLane() noexcept { return chords; }
    BassLane& bassLane() noexcept { return bass; }
    DrumLane& drumLane() noexcept { return drums; }
    ExportTiles& exportTiles() noexcept { return tiles; }
    PlayheadOverlay& playhead() noexcept { return overlay; }
    ChannelHeader& header (ChannelId);

    /** The panel the chord lane opens for `slot` (shown in a call-out box by a click). */
    std::unique_ptr<AlternativesPanel> makeAlternatives (int slot);
    void togglePlay();
    std::function<void (const juce::String&)> onStatus;
    std::function<void()> onSongChanged; // parts or mixer changed (transport bar refresh)
    std::function<void()> onRecordKey;   // R

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    static constexpr int headerWidth = 180, tilesWidth = 220, sourceHeight = 130, stemsHeight = 52,
                         chordHeight = 60, bassHeight = 80, drumHeight = 84;

private:
    void timerCallback() override;
    void showAlternatives (int slot);
    void showFx (ChannelId);
    juce::Rectangle<int> lanesArea() const;

    SongController* song = nullptr;
    TimelineModel model;
    SourceLane source { model };
    ChordLane chords { model };
    BassLane bass { model };
    DrumLane drums { model };
    PlayheadOverlay overlay { model };
    std::vector<std::unique_ptr<ChannelHeader>> headers; // every channel, in ChannelId order
    ExportTiles tiles;
};

} // namespace jaudios::gui
