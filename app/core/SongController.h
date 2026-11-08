#pragma once

#include "AudioEngine.h"
#include "ExportService.h"
#include "SongPipeline.h"

#include "engine/export/Export.h"

#include <juce_data_structures/juce_data_structures.h>

namespace jaudios
{

/** The song the app is editing (ui-shell): the analysed source, the chosen progression with the
    user's chord replacements, the style and the mixer, kept in a ValueTree with an UndoManager.
    Every change, made by an action or by undo / redo, reaches the engine through the tree's
    listener: new clips for the generated channels, mixer settings for the strips. The UI only
    reads the controller and calls its actions. Message thread. */
class SongController final : private juce::ValueTree::Listener
{
public:
    SongController (AudioEngine&, std::shared_ptr<const SongAnalysis>, std::shared_ptr<const juce::AudioBuffer<float>> source,
                    generators::Style);
    ~SongController() override;

    const SongAnalysis& song() const noexcept { return *analysed; }
    const SongParts& parts() const noexcept { return current; }
    const exporting::BarGrid& grid() const noexcept { return barGrid; }
    double durationSeconds() const noexcept { return analysed->durationSec; }
    juce::ValueTree& state() noexcept { return tree; }
    juce::UndoManager& undoManager() noexcept { return undo; }
    void setSongName (const juce::String& n) { name = n; }   // export file names
    juce::String songName() const { return name.isNotEmpty() ? name : juce::String ("Song"); }
    AudioEngine& audioEngine() noexcept { return engine; }

    // --- actions (each one undoable step) -----------------------------------------------
    generators::Style style() const;
    void setStyle (generators::Style);                  // new suggestions; drops the chord replacements
    int progressionIndex() const;
    int numProgressions() const noexcept { return (int) analysed->progressions.size(); }
    const generators::Progression& suggestion (int index) const { return analysed->progressions[(size_t) index]; }
    void selectProgression (int index);                 // drops the chord replacements
    /** The progression playing: the selected suggestion with the user's replacements. */
    const generators::Progression& currentProgression() const noexcept { return current.progression; }
    std::vector<generators::Alternative> alternativesFor (int slot) const;
    void replaceChord (int slot, const theory::Chord&);
    void resetChord (int slot);
    bool isReplaced (int slot) const;

    float gainDecibels (ChannelId) const;
    bool isMuted (ChannelId) const;
    bool isSoloed (ChannelId) const;
    void setGainDecibels (ChannelId, float dB);
    void setMute (ChannelId, bool);
    void setSolo (ChannelId, bool);

    // --- export tiles ---------------------------------------------------------------------
    /** A part as a file in `dir` for dragging into a DAW: "chords" / "bass" / "drums" / "all" as
        type-1 MIDI, or any export part (mix, chords, stems...) as a 24-bit WAV with a tail, rendered
        offline through a second engine set up like this one (same instruments, FX and mixer). */
    juce::File exportMidi (const juce::String& part, const juce::File& dir) const;
    juce::File exportWav (const juce::String& part, const juce::File& dir, double tailSeconds = 2.0) const;

    std::function<void()> onPartsChanged;               // the lanes repaint
    std::function<void()> onMixerChanged;               // the strips update

    static constexpr int channelCount = numChannelIds;

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void rebuildParts();
    void applyMixer();
    juce::ValueTree channelNode (ChannelId) const;
    juce::ValueTree chordsNode() const;
    void setInstrumentsForStyle();

    AudioEngine& engine;
    std::shared_ptr<SongAnalysis> analysed;
    std::shared_ptr<const juce::AudioBuffer<float>> sourceBuffer;
    exporting::BarGrid barGrid;
    SongParts current;
    generators::Style suggestedFor;
    std::string chordsPreset, bassPreset;
    juce::String name;
    juce::ValueTree tree;
    juce::UndoManager undo;
    bool applying = false;
};

/** The default built-in instruments of a style (CLI and app agree). */
std::string defaultChordsPreset (generators::Style);
std::string defaultBassPreset (generators::Style);

} // namespace jaudios
