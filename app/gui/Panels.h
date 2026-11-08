#pragma once

#include "app/core/SongController.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jaudios::gui
{

/** A channel's controls at the head of its lane: name, mute, solo, gain, FX. Everything goes
    through the SongController (undoable); FX opens the strip's parameters. */
class ChannelHeader final : public juce::Component
{
public:
    ChannelHeader (ChannelId, const juce::String& name, juce::Colour);
    void setSong (SongController*);
    void refresh (); // from the controller (after undo, ...)
    std::function<void (ChannelId)> onShowFx;
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::Slider& gainSlider() noexcept { return gain; }
    juce::TextButton& muteButton() noexcept { return mute; }
    juce::TextButton& soloButton() noexcept { return solo; }

private:
    ChannelId id;
    juce::String name;
    juce::Colour colour;
    SongController* song = nullptr;
    juce::TextButton mute { "M" }, solo { "S" }, fx { "FX" };
    juce::Slider gain { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
};

/** A small keyboard (two octaves from C3) highlighting a chord's notes. */
class PianoKeys final : public juce::Component
{
public:
    void setNotes (std::vector<int> pitchClassesRootFirst);
    void paint (juce::Graphics&) override;

private:
    std::vector<int> notes;
};

/** The alternatives for one chord slot (the chord lane's popup): name, Roman numeral and how the
    whole progression scores with it; hovering shows it on the piano, clicking puts it in. */
class AlternativesPanel final : public juce::Component
{
public:
    AlternativesPanel (SongController&, int slot);
    int numRows() const noexcept { return (int) alts.size(); }
    /** Puts row `row` in the slot (row -1 = back to the suggestion). */
    void choose (int row);
    std::function<void()> onChosen;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    int rowAt (int y) const;
    SongController& song;
    int slot;
    std::vector<generators::Alternative> alts;
    PianoKeys piano;
    juce::TextButton reset { "Back to the suggestion" };
    int hover = -1;
    static constexpr int rowHeight = 26, headerHeight = 30;
};

/** Export tiles: a row per part, MIDI and WAV tiles; drag a tile into a DAW (the file is written
    into the session's drag folder as the drag starts). */
class ExportTiles final : public juce::Component
{
public:
    ExportTiles();
    void setSong (SongController*, const juce::File& dragFolder);
    struct Tile { juce::String part, label, kind; }; // kind "midi" / "wav"
    const std::vector<Tile>& tiles() const noexcept { return tileList; }
    juce::Rectangle<int> tileBounds (int index) const;
    int tileAt (juce::Point<int>) const;
    /** Writes the tile's file (MIDI now, WAV rendered offline) and returns it. */
    juce::File fileFor (int index);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void (const juce::String&)> onStatus;

private:
    SongController* song = nullptr;
    juce::File folder;
    std::vector<Tile> tileList;
    int pressed = -1;
    bool dragStarted = false;
};

} // namespace jaudios::gui
